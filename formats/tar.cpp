#include "tar.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../leanify.h"
#include "../utils.h"

using std::cerr;
using std::endl;
using std::string;

namespace {

int CalcChecksum(uint8_t* header) {
  // The checksum bytes are treated as spaces
  // ' ' = 32, 32x8 = 256
  int sum = 256;
  for (int i = 0; i < 148; i++)
    sum += header[i];

  for (int i = 156; i < 512; i++)
    sum += header[i];

  return sum;
}

}  // namespace

Tar::Tar(void* p, size_t s /*= 0*/) : Format(p, s) {
  // check file size first
  is_valid_ = s > 512 && s % 512 == 0 && CalcChecksum(fp_) == strtol(static_cast<char*>(p) + 148, nullptr, 8);
}

size_t Tar::Leanify(size_t size_leanified /*= 0*/) {
  if (!is_valid_)
    return Format::Leanify(size_leanified);

  uint8_t* p_read = fp_;
  fp_ -= size_leanified;
  uint8_t* p_write = fp_;
  depth++;

  do {
    int checksum = CalcChecksum(p_read);
    // 256 means the record is all 0
    if (checksum == 256)
      break;

    memmove(p_write, p_read, 512);

    p_read += 512;
    if (checksum != strtol(reinterpret_cast<char*>(p_write) + 148, nullptr, 8)) {
      cerr << "Checksum does not match!" << endl;
      p_write += 512;
      continue;
    }
    char type = *(p_write + 156);
    size_t original_size = strtol(reinterpret_cast<char*>(p_write) + 124, nullptr, 8);
    // align to 512
    size_t size_aligned = (original_size + 0x1FF) & ~0x1FF;
    if (original_size) {
      if ((type == 0 || type == '0') && depth <= max_depth) {
        // normal file
        char* filename = reinterpret_cast<char*>(p_write);
        PrintFileName(filename);

        size_t new_size = LeanifyFile(p_read, original_size, size_leanified, string(filename));
        if (new_size < original_size) {
          // write new size
          sprintf(reinterpret_cast<char*>(p_write) + 124, "%011o", (unsigned int)new_size);

          // update checksum
          sprintf(reinterpret_cast<char*>(p_write) + 148, "%06o", CalcChecksum(p_write));
          p_write[155] = ' ';

          // align to 512
          size_t new_size_aligned = (new_size + 0x1FF) & ~0x1FF;

          // make sure the rest space is all 0
          memset(p_write + new_size + 512, 0, new_size_aligned - new_size);

          p_write += new_size_aligned;
          size_leanified += size_aligned - new_size_aligned;
        } else {
          // update checksum
          sprintf(reinterpret_cast<char*>(p_write) + 148, "%06o", CalcChecksum(p_write));
          p_write[155] = ' ';

          // make sure the rest space is all 0
          memset(p_write + new_size + 512, 0, size_aligned - new_size);
          p_write += size_aligned;
        }
      } else {
        // other type, just move it
        memmove(p_write + 512, p_read, size_aligned);
        p_write += size_aligned;
      }
      p_read += size_aligned;
    }
    p_write += 512;

  } while (p_write < fp_ + size_);

  depth--;

  // write 2 more zero-filled records
  memset(p_write, 0, 1024);
  size_ = p_write + 1024 - fp_;
  return size_;
}
