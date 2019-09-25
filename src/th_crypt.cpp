
#include <cstring>
#include <vector>
#include <algorithm>

#include <boost/utility.hpp>
#include <boost/assert.hpp>

#include "th_crypt.h"

#pragma pack(push, 4)
#pragma warning(push)
#pragma warning(disable: 4625)
#pragma warning(disable: 4626)
class BitReader : boost::noncopyable {
public:
   BitReader(const unsigned char* start, unsigned int size) :
      start(start), size(size), pos(0)
   {
      BOOST_ASSERT(start != NULL);
   }
   ~BitReader() {
   }

   unsigned int ReadBit(unsigned int len) {
      BOOST_ASSERT(len <= 32);
      unsigned int bit_pos = pos % 8;
      unsigned int index = 0;
      const unsigned char* const max = start + (pos + len) / 8 + 1;
      unsigned int result = 0;
      unsigned int value = static_cast<unsigned int>(1) << (len - 1);
      for (const unsigned char* p = start + pos / 8; p < max; p++) {
         while (bit_pos < 8) {
            if (p < start + size && (*p & (1 << (7 - bit_pos)))) {
               result |= value;
            }
            value >>= 1;
            bit_pos++;
            index++;
         }
         bit_pos = 0;
      }

      pos += len;
      return result;
   }
   bool ReadBit(void) {
      return ReadBit(1) == 1;
   }
   char ReadChar(void) {
      return static_cast<char>(ReadBit(8));
   }
   unsigned char ReadUChar(void) {
      return static_cast<unsigned char>(ReadBit(8));
   }

private:
   const unsigned char* const start;
   const unsigned int size;
   unsigned int pos;
};
#pragma warning(pop)
#pragma pack(pop)

#define DICT_SIZE 0x2000
void decomp(const unsigned char* in, unsigned int in_size, unsigned char* out, unsigned int out_size) {
   BOOST_ASSERT(in != NULL);
   BOOST_ASSERT(out != NULL);
   unsigned char dict[DICT_SIZE], * o;
   unsigned int dictop = 1;
   BitReader bit(in, in_size);
   o = out;
   memset(dict, 0, DICT_SIZE);
   while (1) {
      if (bit.ReadBit()) {
         const unsigned char value = bit.ReadUChar();
         if (static_cast<unsigned int>(o - out) >= out_size) {
            return;
         }
         *o = value;
         o++;
         dict[dictop % DICT_SIZE] = value;
         dictop++;
      } else {
         const unsigned int ofs = bit.ReadBit(13);
         if (!ofs) {
            return;
         }
         const unsigned int len = bit.ReadBit(4) + 3;
         for (unsigned int i = 0; i < len; i++) {
            const unsigned char value = dict[(ofs + i) % DICT_SIZE];
            if (static_cast<unsigned int>(o - out) >= out_size) {
               return;
            }
            *o = value;
            o++;
            dict[dictop % DICT_SIZE] = value;
            dictop++;
         }
      }
   }
}

void thcrypter(unsigned char* in, unsigned int in_size, unsigned char key, unsigned char step, unsigned int block, unsigned int limit) {
   const unsigned int temp = in_size % block;
   const unsigned int dec_size = (in_size % 2) + ((temp < block / 4) ? temp : 0);
   if (in_size <= dec_size) {
      return;
   }
   const unsigned int crypt_size = in_size - dec_size;
   std::vector<unsigned char> out(crypt_size);
   unsigned char* in_cur = in;
   unsigned char* out_cur = &out.front();
   const unsigned char* out_end = (std::min)(out_cur + crypt_size, out_cur + limit);
   while (out_cur < out_end) {
      const unsigned int block_size = (std::min)(static_cast<unsigned int>(out_end - out_cur), block);
      const unsigned int increment = block_size / 2 + ((block_size % 2) ? 1 : 0);
      unsigned char* p;
      for (p = out_cur + block_size - 1; p > out_cur; p -= 2) {
         p[0] = static_cast<unsigned char>(in_cur[0] ^ key);
         p[-1] = static_cast<unsigned char>(in_cur[increment] ^ (key + step * increment));
         in_cur++;
         key += step;
      }

      if (block_size % 2 == 1) {
         p[0] = static_cast<unsigned char>(in_cur[0] ^ key);
         key += step;
      }

      key += static_cast<unsigned char>(step * increment);
      in_cur += increment;
      out_cur += block_size;
   }

   memcpy_s(in, in_size, &out.front(), static_cast<unsigned int>(out_cur - &out.front()));
}
