#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) {
    _capacity = capacity;
    _buffer.reserve(capacity);
}

size_t ByteStream::write(const string &data) {
    size_t now = _buffer.size();
    std::vector<char> tmpdata;
    tmpdata.assign(data.begin(), data.end());
    _buffer.insert(_buffer.end(), data.begin(), data.end());
    if (_buffer.size() > _capacity) {
        _buffer.resize(_capacity);
        _bytes_written += _capacity - now;
        return _capacity - now;
    } else {
        _bytes_written += data.size();
        return data.size();
    }
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string res;
    res.clear();
    std::vector<char>::const_iterator its = _buffer.begin();
    res.assign(its, its + len);
    return res;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    std::vector<char>::const_iterator it = _buffer.begin();
    _buffer.erase(it, it + len);
    _bytes_read += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = peek_output(len);
    pop_output(len);
    return res;
}

void ByteStream::end_input() { _end_input = true; }

bool ByteStream::input_ended() const { return _end_input; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const {
    if (buffer_empty() && input_ended())
        return true;
    else
        return false;
}

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer.size(); }
