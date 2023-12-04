#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!active())
        return;
    _time_since_last_segment_received = 0;
    bool need_empty_ack = seg.length_in_sequence_space();
    auto &header = seg.header();
    if (header.rst)
        return abort();

    if (header.ack) {
        if (!_sender.ack_received(header.ackno, header.win)) {
            _sender.send_empty_segment();
            return;
        }
        if (!_sender.segments_out().empty())
            need_empty_ack = false;
    }

    need_empty_ack |= !_receiver.segment_received(seg);

    // Your code here.
    // Consider different cases including ACK packet, RST packet, etc.

    // State transitions (modify according to your situation)
    // If SYN_RECV to SYN_ACKED in LISEN state
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }

    // Check if waiting is necessary upon TCP disconnection
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    // Server-side initiates disconnection
    // CLOSED
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _is_active = false;
        return;
    }

    // If received packet contains no data, it might just be a keep-alive
    if (need_empty_ack && TCPState::state_summary(_receiver) != TCPReceiverStateSummary::LISTEN)
        _sender.send_empty_segment();

    send_segments();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    if (!data.size()) {
        return 0;
    }
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    TCPSegment segment;
    while (!_sender.segments_out().empty()) {
        segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
            segment.header().win = _receiver.window_size();
        }
        _segments_out.push(segment);
    }
    clean_shutdown();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        return send_rst_segment();
    }
    send_segments();
    _time_since_last_segment_received += ms_since_last_tick;
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED &&
        _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        _linger_after_streams_finish = false;
        _is_active = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    send_segments(true);
}

void TCPConnection::connect() { send_segments(true); }

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            send_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_rst_segment() {
    abort();
    TCPSegment seg;
    seg.header().rst = true;
    _segments_out.push(seg);
}

void TCPConnection::abort() {
    _is_active = false;
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
}

void TCPConnection::clean_shutdown() {
    if (!_receiver.stream_out().input_ended()) {
        return;
    }
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }
    if (_sender.stream_in().eof() && _sender.bytes_in_flight() == 0) {
        if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            abort();
        }
    }
}

void TCPConnection::send_segments(bool fill_window) {
    if (fill_window)
        _sender.fill_window();
    auto &segments = _sender.segments_out();
    while (!segments.empty()) {
        auto seg = segments.front();
        if (_receiver.ackno()) {
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
            seg.header().ack = true;
        }
        _segments_out.push(seg);
        segments.pop();
    }
}
