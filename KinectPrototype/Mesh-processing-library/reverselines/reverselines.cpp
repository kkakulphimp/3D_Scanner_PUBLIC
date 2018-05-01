// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "Array.h"
#include "Args.h"
#include "BinaryIO.h"           // write_raw()
using namespace hh;

#if !defined(_WIN32)

#include <sys/mman.h>           // mmap()
#include <sys/stat.h>           // fstat()
#include <fcntl.h>              // open()
#include <cstdlib>              // exit()
#include <cstdio>

int main(int argc, const char** argv) {
    ParseArgs args(argc, argv);
    ARGSC("",                   "filename : output the lines in file in reverse order");
    args.other_args_ok();
    args.parse();
    string filename = args.get_filename();
    if (args.num()) args.problem("expect a single argument");
    int fd = open(filename.c_str(), O_RDONLY); assertx(fd>=0);
    struct stat stat_buf;
    assertx(!fstat(fd, &stat_buf)); // off_t st_size
    unsigned rlen = unsigned(stat_buf.st_size);
    assertx(rlen==stat_buf.st_size); // verify that it fits
    // Had problems with hand0.prog (size 572506964): would not map
    //  into memory, so used segmentation scheme below.
    if (!rlen) {
        Warning("File has zero length");
        return 0;
    }
    const unsigned segsize = 128*1024*1024; // 128 MiB
    unsigned off = 0;
    unsigned len = rlen;
    int i = int(len);           // point right after '\n'
    // Whenever possible, map segment of size [segsize+1, segsize*2]
    while (len>segsize*2) {
        off += segsize;
        len -= segsize;
        i -= segsize;
    }
    CArrayView<char> buf(static_cast<const char*>(mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, off)),
                         len);
    assertx(buf.data()!=reinterpret_cast<void*>(intptr_t{-1}));
    for (;;) {
        if (!i) break;
        assertx(buf[i-1]=='\n');
        int iend = i;
        --i;
        for (;;) {
            if (!i) break;
            if (buf[i-1]=='\n') break;
            --i;
        }
        assertx(write_raw(stdout, buf.slice(i, iend)));
        if (off && unsigned(i)<=segsize) {
            assertx(i);
            assertx(!munmap(const_cast<char*>(buf.data()), len));
            off -= segsize;
            len = segsize*2;
            i += segsize;
            buf.reinit(CArrayView<char>(static_cast<const char*>(mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, off)),
                                        len));
            assertx(buf.data()!=reinterpret_cast<void*>(intptr_t{-1}));
        }
    }
    assertx(off==0);
    assertx(!munmap(const_cast<char*>(buf.data()), len));
    assertx(!HH_POSIX(close)(fd));
    return 0;
}

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>            // CreateFile(), etc.

#if 0

// Conclusion: read() is not any faster than memory-mapped read, even for this mostly sequential access.
// Probably the bottleneck is stdio stream write.

#include "Array.h"
#include <fcntl.h>              // open()

int main(int argc, const char** argv) {
    ParseArgs args(argc, argv);
    ARGSC("",                   "filename : output the lines in file in reverse order");
    args.other_args_ok();
    args.parse();
    string filename = args.get_filename();
    if (args.num()) args.problem("expect a single argument");
    int fd = open(filename.c_str(), O_RDONLY | O_BINARY | O_SEQUENTIAL); assertx(fd>=0);
    int64_t size = _filelengthi64(fd);
    const unsigned segsize = 128*1024*1024; // 128 MiB view
    Array<char> buf(segsize*2);
    int64_t off = 0;
    int64_t llen = size;
    // Whenever possible, map segment of size [segsize+1, segsize*2]
    while (llen>segsize*2) { off += segsize; llen -= segsize; }
    int len = assert_narrow_cast<int>(llen);
    int i = len;
    _lseeki64(fd, off, SEEK_SET); assertx(read(fd, buf.data(), len)==len);
    int nwarnings = 0;
    while (i) {
        assertx(buf[i-1]=='\n');
        int iend = i;
        if (i>1 && buf[i-2]=='\r') {
            if (!nwarnings++) showf("Warning: found '\\r' in end-of-line\n");
            --i;
        }
        --i;
        while (i) {
            assertx(buf[i-1]!='\r');
            if (buf[i-1]=='\n') break;
            --i;
        }
        assertx(write_raw(stdout, buf.slice(i, iend)));
        if (off && i<=segsize) {
            assertx(i);
            off -= segsize;
            len = segsize*2;
            i += segsize;
            _lseeki64(fd, off, SEEK_SET); assertx(read(fd, buf.data(), len)==len);
        }
    }
    assertx(off==0);
    assertx(!HH_POSIX(close)(fd));
    fflush(stdout);
    return 0;
}

#else

// Output the text file named "file" with lines in reverse order
//  to stdout by memory mapping the file.

int main(int argc, const char** argv) {
    ParseArgs args(argc, argv);
    ARGSC("",                   "filename : output the lines in file in reverse order");
    args.other_args_ok();
    args.parse();
    string filename = args.get_filename();
    if (args.num()) args.problem("expect a single argument");
    // 0=no_security,  0=no_copy_attribute_from_existing_fhandle
    HANDLE h_file = CreateFileW(widen(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    assertx(h_file!=INVALID_HANDLE_VALUE);
    // CreateFileMapping fails on files of size 0, so test it here.
    int64_t size; {
        LARGE_INTEGER li; assertx(GetFileSizeEx(h_file, &li));
        size = li.QuadPart;
    }
    if (!size) { Warning("File has zero length"); assertx(CloseHandle(h_file)); return 0; }
    // 0=no_security,  0,0=map_whole_file,  0=no_name
    HANDLE h_fmapping = CreateFileMapping(h_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    assertx(h_fmapping!=nullptr); HH_ASSUME(h_fmapping);
    const unsigned segsize = 128*1024*1024; // 128 MiB view
    int64_t off = 0;
    int64_t llen = size;
    // Whenever possible, map segment of size [segsize+1, segsize*2]
    while (llen>segsize*2) { off += segsize; llen -= segsize; }
    int len = int(llen);
    int i = len;
    CArrayView<char> buf(static_cast<const char*>(MapViewOfFile(h_fmapping, FILE_MAP_READ, DWORD(off>>32),
                                                                DWORD(off), DWORD(len))),
                         len);
    assertx(buf.data());
    int nwarnings = 0;
    while (i) {
        assertx(buf[i-1]=='\n');
        int iend = i;
        if (i>1 && buf[i-2]=='\r') {
            if (!nwarnings++) showf("Warning: found '\\r' in end-of-line\n");
            --i;
        }
        --i;
        while (i) {
            assertx(buf[i-1]!='\r');
            if (buf[i-1]=='\n') break;
            --i;
        }
        assertx(write_raw(stdout, buf.slice(i, iend)));
        if (off && unsigned(i)<=segsize) {
            assertx(i);
            assertx(UnmapViewOfFile(buf.data()));
            off -= segsize;
            len = segsize*2;
            i += segsize;
            buf.reinit(CArrayView<char>(static_cast<const char*>(MapViewOfFile(h_fmapping, FILE_MAP_READ,
                                                                               DWORD(off>>32),
                                                                               DWORD(off), DWORD(len))),
                                        len));
            assertx(buf.data());
        }
    }
    assertx(off==0);
    assertx(UnmapViewOfFile(buf.data()));
    assertx(CloseHandle(h_fmapping));
    assertx(CloseHandle(h_file));
    fflush(stdout);
    return 0;
}

#endif  // 0

#endif  // !defined(_WIN32)
