#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>
// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _rto{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _byte_in_flight; }

void TCPSender::fill_window() {
    TCPSegment seg;
    if (_send_syn == 0) {
        _send_syn = 1;
        seg.header().syn = true;
        seg.header().seqno = wrap(_next_seqno, _isn);
        size_t len = seg.length_in_sequence_space();
        _next_seqno += len;
        _byte_in_flight += len;
        _remaining_size -= len;
        _segments_out.push(seg);
        _segments_sended.push(seg);
        if (!_is_ticking) {
            _is_ticking = true;
            _time_elapsed = 0;
        }
        return;
    }
    if (!_segments_sended.empty() && _segments_sended.front().header().syn)
        return;
    if (!stream_in().buffer_size() && !stream_in().eof())
        return;
    if (_send_fin)
        return;
    if (_window_size && _remaining_size != 0) {
        while (_remaining_size) {
            size_t byte_len =
                min(stream_in().buffer_size(),
                    min(static_cast<size_t>(_remaining_size), static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)));
            seg.payload() = stream_in().read(byte_len);
            if (stream_in().eof() && static_cast<size_t>(_remaining_size) > byte_len) {
                seg.header().fin = 1;
                _send_fin = 1;
            }
            seg.header().seqno = wrap(_next_seqno, _isn);
            size_t len = seg.length_in_sequence_space();
            _next_seqno += len;
            _byte_in_flight += len;
            if (_send_syn)
                _remaining_size -= len;
            _segments_out.push(seg);
            _segments_sended.push(seg);
            if (!_is_ticking) {
                _is_ticking = true;
                _time_elapsed = 0;
            }
            if (stream_in().buffer_empty())
                break;
        }
    } else if (_remaining_size == 0) {
        if (stream_in().eof() && (bytes_in_flight() < _window_size || _window_size == 0)) {
            while (!_segments_out.empty())
                _segments_out.pop();
            while (!_segments_sended.empty())
                _segments_sended.pop();
            seg.header().fin = 1;
            _send_fin = 1;
            seg.header().seqno = wrap(_next_seqno, _isn);
            size_t len = seg.length_in_sequence_space();
            _next_seqno += len;
            _byte_in_flight += len;
            if (_send_syn)
                _remaining_size -= len;
            _segments_out.push(seg);
            _segments_sended.push(seg);
            if (!_is_ticking) {
                _is_ticking = true;
                _time_elapsed = 0;
            }
        } else if (!stream_in().buffer_empty() && _window_size == 0) {
            seg.payload() = stream_in().read(1);
            seg.header().seqno = wrap(_next_seqno, _isn);
            if (stream_in().eof() && static_cast<size_t>(_remaining_size) > 0) {
                seg.header().fin = 1;
                _send_fin = 1;
            }
            size_t len = seg.length_in_sequence_space();
            _next_seqno += len;
            _byte_in_flight += len;
            if (_send_syn)
                _remaining_size -= len;
            _segments_out.push(seg);
            _segments_sended.push(seg);
            if (!_is_ticking) {
                _is_ticking = true;
                _time_elapsed = 0;
            }
        }
    }
    // cout << _window_size << " " << _byte_in_flight << endl;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (abs_ackno > _next_seqno)
        return false;

    _window_size = window_size;
    // if (abs_ackno <= _ack_seq) return true;
    _ack_seq = abs_ackno;
    _send_syn = true;
    _window_size = window_size;
    _remaining_size = window_size;
    while (!_segments_sended.empty()) {
        TCPSegment seg = _segments_sended.front();
        if (unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= abs_ackno) {
            _byte_in_flight -= seg.length_in_sequence_space();
            _segments_sended.pop();
            _time_elapsed = 0;
            _rto = _initial_retransmission_timeout;
            _counter_consecutive_retransmissions = 0;
        } else
            break;
    }
    if (!_segments_sended.empty()) {
        _remaining_size =
            static_cast<uint16_t>(abs_ackno + static_cast<uint64_t>(window_size) -
                                  unwrap(_segments_sended.front().header().seqno, _isn, _next_seqno) - _byte_in_flight);
    }
    if (!_byte_in_flight)
        _is_ticking = 0;
    fill_window();
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_is_ticking == 0)
        return;
    _time_elapsed += ms_since_last_tick;
    if (_time_elapsed >= _rto) {
        _segments_out.push(_segments_sended.front());
        if (_window_size || _segments_sended.front().header().syn) {
            ++_counter_consecutive_retransmissions;
            _rto <<= 1;
        }
        _time_elapsed = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _counter_consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
