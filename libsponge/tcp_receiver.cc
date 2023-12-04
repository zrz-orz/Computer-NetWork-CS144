#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    auto &header = seg.header();
    if (!_get_syn && !header.syn)
        return false;
    if (_reassembler.stream_out().input_ended() && header.fin)
        return false;
    if (header.syn) {
        if (_get_syn)
            return false;
        _isn = header.seqno;
        _get_syn = true;
        if (header.fin) {
            _reassembler.push_substring(seg.payload().copy(), 0, true);
            return true;
        }
        if (seg.length_in_sequence_space() != 0) {
            _reassembler.push_substring(seg.payload().copy(), 0, false);
            return true;
        }
    }
    size_t seg_len = max(seg.length_in_sequence_space(), 1UL);
    _checkpoint = unwrap(header.seqno, _isn, _checkpoint);
    uint64_t index = _checkpoint - 1;
    uint64_t unaccept_index = max(window_size(), 1UL) + _reassembler.next_index();
    if (seg_len + index <= _reassembler.next_index() || index >= unaccept_index)
        return false;
    _reassembler.push_substring(seg.payload().copy(), index, header.fin);
    return true;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_get_syn)
        return optional<WrappingInt32>();
    return wrap(_reassembler.next_index() + 1 + _reassembler.stream_out().input_ended(), _isn);
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
