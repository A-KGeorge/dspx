#pragma once
#include <vector>
#include <cstring>
#include <string>
#include <stdexcept>

namespace dsp
{
    namespace toon
    {

        // TOON Protocol Tokens
        enum Token : uint8_t
        {
            T_NULL = 0x00,
            T_INT32 = 0x01,
            T_FLOAT = 0x02,
            T_STRING = 0x03,
            T_FLOAT_ARRAY = 0x04, // Optimized for DSP buffers
            T_OBJECT_START = 0x10,
            T_OBJECT_END = 0x11,
            T_ARRAY_START = 0x12,
            T_ARRAY_END = 0x13,
            T_BOOL = 0x14,
            T_DOUBLE = 0x15
        };

        class Serializer
        {
        public:
            std::vector<uint8_t> buffer;

            void writeTag(Token tag)
            {
                buffer.push_back(static_cast<uint8_t>(tag));
            }

            void writeInt32(int32_t val)
            {
                writeTag(T_INT32);
                appendRaw(&val, sizeof(val));
            }

            void writeFloat(float val)
            {
                writeTag(T_FLOAT);
                appendRaw(&val, sizeof(val));
            }

            void writeDouble(double val)
            {
                writeTag(T_DOUBLE);
                appendRaw(&val, sizeof(val));
            }

            void writeBool(bool val)
            {
                writeTag(T_BOOL);
                uint8_t byte = val ? 1 : 0;
                buffer.push_back(byte);
            }

            void writeString(const std::string &val)
            {
                writeTag(T_STRING);
                int32_t len = static_cast<int32_t>(val.length());
                appendRaw(&len, sizeof(len)); // Length prefix
                appendRaw(val.data(), len);
            }

            // Critical Optimization: Zero-copy-like write for DSP buffers
            void writeFloatArray(const std::vector<float> &data)
            {
                writeTag(T_FLOAT_ARRAY);
                int32_t count = static_cast<int32_t>(data.size());
                appendRaw(&count, sizeof(count));
                if (count > 0)
                {
                    appendRaw(data.data(), count * sizeof(float));
                }
            }

            void startObject() { writeTag(T_OBJECT_START); }
            void endObject() { writeTag(T_OBJECT_END); }
            void startArray() { writeTag(T_ARRAY_START); }
            void endArray() { writeTag(T_ARRAY_END); }

        private:
            void appendRaw(const void *ptr, size_t size)
            {
                const uint8_t *bytes = static_cast<const uint8_t *>(ptr);
                buffer.insert(buffer.end(), bytes, bytes + size);
            }
        };

        class Deserializer
        {
        public:
            const uint8_t *data;
            size_t size;
            size_t pos;

            Deserializer(const uint8_t *d, size_t s) : data(d), size(s), pos(0) {}

            Token peekToken()
            {
                if (pos >= size)
                    return T_NULL;
                return static_cast<Token>(data[pos]);
            }

            Token readToken()
            {
                if (pos >= size)
                    throw std::runtime_error("TOON: Unexpected EOF");
                return static_cast<Token>(data[pos++]);
            }

            int32_t readInt32()
            {
                consumeToken(T_INT32);
                return readRaw<int32_t>();
            }

            float readFloat()
            {
                consumeToken(T_FLOAT);
                return readRaw<float>();
            }

            double readDouble()
            {
                consumeToken(T_DOUBLE);
                return readRaw<double>();
            }

            bool readBool()
            {
                consumeToken(T_BOOL);
                if (pos >= size)
                    throw std::runtime_error("TOON: EOF reading bool");
                return data[pos++] != 0;
            }

            std::string readString()
            {
                consumeToken(T_STRING);
                int32_t len = readRaw<int32_t>();
                if (pos + len > size)
                    throw std::runtime_error("TOON: String out of bounds");
                std::string s(reinterpret_cast<const char *>(data + pos), len);
                pos += len;
                return s;
            }

            std::vector<float> readFloatArray()
            {
                consumeToken(T_FLOAT_ARRAY);
                int32_t count = readRaw<int32_t>();
                if (count < 0)
                    throw std::runtime_error("TOON: Invalid array count");
                if (pos + count * sizeof(float) > size)
                    throw std::runtime_error("TOON: Array out of bounds");

                std::vector<float> vec(count);
                if (count > 0)
                {
                    std::memcpy(vec.data(), data + pos, count * sizeof(float));
                    pos += count * sizeof(float);
                }
                return vec;
            }

            void consumeToken(Token expected)
            {
                Token actual = readToken();
                if (actual != expected)
                    throw std::runtime_error("TOON: Type mismatch");
            }

        private:
            template <typename T>
            T readRaw()
            {
                if (pos + sizeof(T) > size)
                    throw std::runtime_error("TOON: EOF reading value");
                T val;
                std::memcpy(&val, data + pos, sizeof(T));
                pos += sizeof(T);
                return val;
            }
        };

    } // namespace toon
} // namespace dsp
