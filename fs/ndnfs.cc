/*
 * Copyright (c) 2013 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Wentao Shang <wentao@cs.ucla.edu>
 *         Qiuhan Ding <dingqiuhan@gmail.com>
 *         Zhehao Wang <wangzhehao410305@gmail.com>
 */
 
// Since the file gets stored as Data(Blob) into sqlite, whose name would not be changeable,
// so the fs should not use a different prefix, or all files before that prefix would become unsolicited data.

// TODO:
// 1. Figure out the /private path, and the meaning of two paths; mount point vs actual, their relationships
// 2. Solve the access_mode problem in current implementation: 
//      Right now need to give write access for everyone to the actual dir.
// 3. Double check the functions are using correct paths, and finish implementing truncate and remove functions
// 4. Remove unnecessary fields from the database
// 5. Summarize their original approach, and the problems that I get stuck upon earlier (persistent files; open not called; stuck upon getattr for mount point, etc)
// 6. Asynchronous signing
// My fuse examples:
// http://www.gtoal.com/fusestuff/fuse-2.7.0/example/fusexmp.c.html
// http://fuse.sourceforge.net/doxygen/fusexmp_8c.html
// https://code.google.com/p/fuse-examplefs/source/browse/examplefs.cc

#include "ndnfs.h"
#include "directory.h"
#include "file.h"
#include "attribute.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

using namespace std;
using namespace ndn;

static uint8_t DEFAULT_RSA_PUBLIC_KEY_DER[] = {
  0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01,
  0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01,
  0x00, 0xb8, 0x09, 0xa7, 0x59, 0x82, 0x84, 0xec, 0x4f, 0x06, 0xfa, 0x1c, 0xb2, 0xe1, 0x38, 0x93,
  0x53, 0xbb, 0x7d, 0xd4, 0xac, 0x88, 0x1a, 0xf8, 0x25, 0x11, 0xe4, 0xfa, 0x1d, 0x61, 0x24, 0x5b,
  0x82, 0xca, 0xcd, 0x72, 0xce, 0xdb, 0x66, 0xb5, 0x8d, 0x54, 0xbd, 0xfb, 0x23, 0xfd, 0xe8, 0x8e,
  0xaf, 0xa7, 0xb3, 0x79, 0xbe, 0x94, 0xb5, 0xb7, 0xba, 0x17, 0xb6, 0x05, 0xae, 0xce, 0x43, 0xbe,
  0x3b, 0xce, 0x6e, 0xea, 0x07, 0xdb, 0xbf, 0x0a, 0x7e, 0xeb, 0xbc, 0xc9, 0x7b, 0x62, 0x3c, 0xf5,
  0xe1, 0xce, 0xe1, 0xd9, 0x8d, 0x9c, 0xfe, 0x1f, 0xc7, 0xf8, 0xfb, 0x59, 0xc0, 0x94, 0x0b, 0x2c,
  0xd9, 0x7d, 0xbc, 0x96, 0xeb, 0xb8, 0x79, 0x22, 0x8a, 0x2e, 0xa0, 0x12, 0x1d, 0x42, 0x07, 0xb6,
  0x5d, 0xdb, 0xe1, 0xf6, 0xb1, 0x5d, 0x7b, 0x1f, 0x54, 0x52, 0x1c, 0xa3, 0x11, 0x9b, 0xf9, 0xeb,
  0xbe, 0xb3, 0x95, 0xca, 0xa5, 0x87, 0x3f, 0x31, 0x18, 0x1a, 0xc9, 0x99, 0x01, 0xec, 0xaa, 0x90,
  0xfd, 0x8a, 0x36, 0x35, 0x5e, 0x12, 0x81, 0xbe, 0x84, 0x88, 0xa1, 0x0d, 0x19, 0x2a, 0x4a, 0x66,
  0xc1, 0x59, 0x3c, 0x41, 0x83, 0x3d, 0x3d, 0xb8, 0xd4, 0xab, 0x34, 0x90, 0x06, 0x3e, 0x1a, 0x61,
  0x74, 0xbe, 0x04, 0xf5, 0x7a, 0x69, 0x1b, 0x9d, 0x56, 0xfc, 0x83, 0xb7, 0x60, 0xc1, 0x5e, 0x9d,
  0x85, 0x34, 0xfd, 0x02, 0x1a, 0xba, 0x2c, 0x09, 0x72, 0xa7, 0x4a, 0x5e, 0x18, 0xbf, 0xc0, 0x58,
  0xa7, 0x49, 0x34, 0x46, 0x61, 0x59, 0x0e, 0xe2, 0x6e, 0x9e, 0xd2, 0xdb, 0xfd, 0x72, 0x2f, 0x3c,
  0x47, 0xcc, 0x5f, 0x99, 0x62, 0xee, 0x0d, 0xf3, 0x1f, 0x30, 0x25, 0x20, 0x92, 0x15, 0x4b, 0x04,
  0xfe, 0x15, 0x19, 0x1d, 0xdc, 0x7e, 0x5c, 0x10, 0x21, 0x52, 0x21, 0x91, 0x54, 0x60, 0x8b, 0x92,
  0x41, 0x02, 0x03, 0x01, 0x00, 0x01
};

static uint8_t DEFAULT_RSA_PRIVATE_KEY_DER[] = {
  0x30, 0x82, 0x04, 0xa5, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01, 0x01, 0x00, 0xb8, 0x09, 0xa7, 0x59,
  0x82, 0x84, 0xec, 0x4f, 0x06, 0xfa, 0x1c, 0xb2, 0xe1, 0x38, 0x93, 0x53, 0xbb, 0x7d, 0xd4, 0xac,
  0x88, 0x1a, 0xf8, 0x25, 0x11, 0xe4, 0xfa, 0x1d, 0x61, 0x24, 0x5b, 0x82, 0xca, 0xcd, 0x72, 0xce,
  0xdb, 0x66, 0xb5, 0x8d, 0x54, 0xbd, 0xfb, 0x23, 0xfd, 0xe8, 0x8e, 0xaf, 0xa7, 0xb3, 0x79, 0xbe,
  0x94, 0xb5, 0xb7, 0xba, 0x17, 0xb6, 0x05, 0xae, 0xce, 0x43, 0xbe, 0x3b, 0xce, 0x6e, 0xea, 0x07,
  0xdb, 0xbf, 0x0a, 0x7e, 0xeb, 0xbc, 0xc9, 0x7b, 0x62, 0x3c, 0xf5, 0xe1, 0xce, 0xe1, 0xd9, 0x8d,
  0x9c, 0xfe, 0x1f, 0xc7, 0xf8, 0xfb, 0x59, 0xc0, 0x94, 0x0b, 0x2c, 0xd9, 0x7d, 0xbc, 0x96, 0xeb,
  0xb8, 0x79, 0x22, 0x8a, 0x2e, 0xa0, 0x12, 0x1d, 0x42, 0x07, 0xb6, 0x5d, 0xdb, 0xe1, 0xf6, 0xb1,
  0x5d, 0x7b, 0x1f, 0x54, 0x52, 0x1c, 0xa3, 0x11, 0x9b, 0xf9, 0xeb, 0xbe, 0xb3, 0x95, 0xca, 0xa5,
  0x87, 0x3f, 0x31, 0x18, 0x1a, 0xc9, 0x99, 0x01, 0xec, 0xaa, 0x90, 0xfd, 0x8a, 0x36, 0x35, 0x5e,
  0x12, 0x81, 0xbe, 0x84, 0x88, 0xa1, 0x0d, 0x19, 0x2a, 0x4a, 0x66, 0xc1, 0x59, 0x3c, 0x41, 0x83,
  0x3d, 0x3d, 0xb8, 0xd4, 0xab, 0x34, 0x90, 0x06, 0x3e, 0x1a, 0x61, 0x74, 0xbe, 0x04, 0xf5, 0x7a,
  0x69, 0x1b, 0x9d, 0x56, 0xfc, 0x83, 0xb7, 0x60, 0xc1, 0x5e, 0x9d, 0x85, 0x34, 0xfd, 0x02, 0x1a,
  0xba, 0x2c, 0x09, 0x72, 0xa7, 0x4a, 0x5e, 0x18, 0xbf, 0xc0, 0x58, 0xa7, 0x49, 0x34, 0x46, 0x61,
  0x59, 0x0e, 0xe2, 0x6e, 0x9e, 0xd2, 0xdb, 0xfd, 0x72, 0x2f, 0x3c, 0x47, 0xcc, 0x5f, 0x99, 0x62,
  0xee, 0x0d, 0xf3, 0x1f, 0x30, 0x25, 0x20, 0x92, 0x15, 0x4b, 0x04, 0xfe, 0x15, 0x19, 0x1d, 0xdc,
  0x7e, 0x5c, 0x10, 0x21, 0x52, 0x21, 0x91, 0x54, 0x60, 0x8b, 0x92, 0x41, 0x02, 0x03, 0x01, 0x00,
  0x01, 0x02, 0x82, 0x01, 0x01, 0x00, 0x8a, 0x05, 0xfb, 0x73, 0x7f, 0x16, 0xaf, 0x9f, 0xa9, 0x4c,
  0xe5, 0x3f, 0x26, 0xf8, 0x66, 0x4d, 0xd2, 0xfc, 0xd1, 0x06, 0xc0, 0x60, 0xf1, 0x9f, 0xe3, 0xa6,
  0xc6, 0x0a, 0x48, 0xb3, 0x9a, 0xca, 0x21, 0xcd, 0x29, 0x80, 0x88, 0x3d, 0xa4, 0x85, 0xa5, 0x7b,
  0x82, 0x21, 0x81, 0x28, 0xeb, 0xf2, 0x43, 0x24, 0xb0, 0x76, 0xc5, 0x52, 0xef, 0xc2, 0xea, 0x4b,
  0x82, 0x41, 0x92, 0xc2, 0x6d, 0xa6, 0xae, 0xf0, 0xb2, 0x26, 0x48, 0xa1, 0x23, 0x7f, 0x02, 0xcf,
  0xa8, 0x90, 0x17, 0xa2, 0x3e, 0x8a, 0x26, 0xbd, 0x6d, 0x8a, 0xee, 0xa6, 0x0c, 0x31, 0xce, 0xc2,
  0xbb, 0x92, 0x59, 0xb5, 0x73, 0xe2, 0x7d, 0x91, 0x75, 0xe2, 0xbd, 0x8c, 0x63, 0xe2, 0x1c, 0x8b,
  0xc2, 0x6a, 0x1c, 0xfe, 0x69, 0xc0, 0x44, 0xcb, 0x58, 0x57, 0xb7, 0x13, 0x42, 0xf0, 0xdb, 0x50,
  0x4c, 0xe0, 0x45, 0x09, 0x8f, 0xca, 0x45, 0x8a, 0x06, 0xfe, 0x98, 0xd1, 0x22, 0xf5, 0x5a, 0x9a,
  0xdf, 0x89, 0x17, 0xca, 0x20, 0xcc, 0x12, 0xa9, 0x09, 0x3d, 0xd5, 0xf7, 0xe3, 0xeb, 0x08, 0x4a,
  0xc4, 0x12, 0xc0, 0xb9, 0x47, 0x6c, 0x79, 0x50, 0x66, 0xa3, 0xf8, 0xaf, 0x2c, 0xfa, 0xb4, 0x6b,
  0xec, 0x03, 0xad, 0xcb, 0xda, 0x24, 0x0c, 0x52, 0x07, 0x87, 0x88, 0xc0, 0x21, 0xf3, 0x02, 0xe8,
  0x24, 0x44, 0x0f, 0xcd, 0xa0, 0xad, 0x2f, 0x1b, 0x79, 0xab, 0x6b, 0x49, 0x4a, 0xe6, 0x3b, 0xd0,
  0xad, 0xc3, 0x48, 0xb9, 0xf7, 0xf1, 0x34, 0x09, 0xeb, 0x7a, 0xc0, 0xd5, 0x0d, 0x39, 0xd8, 0x45,
  0xce, 0x36, 0x7a, 0xd8, 0xde, 0x3c, 0xb0, 0x21, 0x96, 0x97, 0x8a, 0xff, 0x8b, 0x23, 0x60, 0x4f,
  0xf0, 0x3d, 0xd7, 0x8f, 0xf3, 0x2c, 0xcb, 0x1d, 0x48, 0x3f, 0x86, 0xc4, 0xa9, 0x00, 0xf2, 0x23,
  0x2d, 0x72, 0x4d, 0x66, 0xa5, 0x01, 0x02, 0x81, 0x81, 0x00, 0xdc, 0x4f, 0x99, 0x44, 0x0d, 0x7f,
  0x59, 0x46, 0x1e, 0x8f, 0xe7, 0x2d, 0x8d, 0xdd, 0x54, 0xc0, 0xf7, 0xfa, 0x46, 0x0d, 0x9d, 0x35,
  0x03, 0xf1, 0x7c, 0x12, 0xf3, 0x5a, 0x9d, 0x83, 0xcf, 0xdd, 0x37, 0x21, 0x7c, 0xb7, 0xee, 0xc3,
  0x39, 0xd2, 0x75, 0x8f, 0xb2, 0x2d, 0x6f, 0xec, 0xc6, 0x03, 0x55, 0xd7, 0x00, 0x67, 0xd3, 0x9b,
  0xa2, 0x68, 0x50, 0x6f, 0x9e, 0x28, 0xa4, 0x76, 0x39, 0x2b, 0xb2, 0x65, 0xcc, 0x72, 0x82, 0x93,
  0xa0, 0xcf, 0x10, 0x05, 0x6a, 0x75, 0xca, 0x85, 0x35, 0x99, 0xb0, 0xa6, 0xc6, 0xef, 0x4c, 0x4d,
  0x99, 0x7d, 0x2c, 0x38, 0x01, 0x21, 0xb5, 0x31, 0xac, 0x80, 0x54, 0xc4, 0x18, 0x4b, 0xfd, 0xef,
  0xb3, 0x30, 0x22, 0x51, 0x5a, 0xea, 0x7d, 0x9b, 0xb2, 0x9d, 0xcb, 0xba, 0x3f, 0xc0, 0x1a, 0x6b,
  0xcd, 0xb0, 0xe6, 0x2f, 0x04, 0x33, 0xd7, 0x3a, 0x49, 0x71, 0x02, 0x81, 0x81, 0x00, 0xd5, 0xd9,
  0xc9, 0x70, 0x1a, 0x13, 0xb3, 0x39, 0x24, 0x02, 0xee, 0xb0, 0xbb, 0x84, 0x17, 0x12, 0xc6, 0xbd,
  0x65, 0x73, 0xe9, 0x34, 0x5d, 0x43, 0xff, 0xdc, 0xf8, 0x55, 0xaf, 0x2a, 0xb9, 0xe1, 0xfa, 0x71,
  0x65, 0x4e, 0x50, 0x0f, 0xa4, 0x3b, 0xe5, 0x68, 0xf2, 0x49, 0x71, 0xaf, 0x15, 0x88, 0xd7, 0xaf,
  0xc4, 0x9d, 0x94, 0x84, 0x6b, 0x5b, 0x10, 0xd5, 0xc0, 0xaa, 0x0c, 0x13, 0x62, 0x99, 0xc0, 0x8b,
  0xfc, 0x90, 0x0f, 0x87, 0x40, 0x4d, 0x58, 0x88, 0xbd, 0xe2, 0xba, 0x3e, 0x7e, 0x2d, 0xd7, 0x69,
  0xa9, 0x3c, 0x09, 0x64, 0x31, 0xb6, 0xcc, 0x4d, 0x1f, 0x23, 0xb6, 0x9e, 0x65, 0xd6, 0x81, 0xdc,
  0x85, 0xcc, 0x1e, 0xf1, 0x0b, 0x84, 0x38, 0xab, 0x93, 0x5f, 0x9f, 0x92, 0x4e, 0x93, 0x46, 0x95,
  0x6b, 0x3e, 0xb6, 0xc3, 0x1b, 0xd7, 0x69, 0xa1, 0x0a, 0x97, 0x37, 0x78, 0xed, 0xd1, 0x02, 0x81,
  0x80, 0x33, 0x18, 0xc3, 0x13, 0x65, 0x8e, 0x03, 0xc6, 0x9f, 0x90, 0x00, 0xae, 0x30, 0x19, 0x05,
  0x6f, 0x3c, 0x14, 0x6f, 0xea, 0xf8, 0x6b, 0x33, 0x5e, 0xee, 0xc7, 0xf6, 0x69, 0x2d, 0xdf, 0x44,
  0x76, 0xaa, 0x32, 0xba, 0x1a, 0x6e, 0xe6, 0x18, 0xa3, 0x17, 0x61, 0x1c, 0x92, 0x2d, 0x43, 0x5d,
  0x29, 0xa8, 0xdf, 0x14, 0xd8, 0xff, 0xdb, 0x38, 0xef, 0xb8, 0xb8, 0x2a, 0x96, 0x82, 0x8e, 0x68,
  0xf4, 0x19, 0x8c, 0x42, 0xbe, 0xcc, 0x4a, 0x31, 0x21, 0xd5, 0x35, 0x6c, 0x5b, 0xa5, 0x7c, 0xff,
  0xd1, 0x85, 0x87, 0x28, 0xdc, 0x97, 0x75, 0xe8, 0x03, 0x80, 0x1d, 0xfd, 0x25, 0x34, 0x41, 0x31,
  0x21, 0x12, 0x87, 0xe8, 0x9a, 0xb7, 0x6a, 0xc0, 0xc4, 0x89, 0x31, 0x15, 0x45, 0x0d, 0x9c, 0xee,
  0xf0, 0x6a, 0x2f, 0xe8, 0x59, 0x45, 0xc7, 0x7b, 0x0d, 0x6c, 0x55, 0xbb, 0x43, 0xca, 0xc7, 0x5a,
  0x01, 0x02, 0x81, 0x81, 0x00, 0xab, 0xf4, 0xd5, 0xcf, 0x78, 0x88, 0x82, 0xc2, 0xdd, 0xbc, 0x25,
  0xe6, 0xa2, 0xc1, 0xd2, 0x33, 0xdc, 0xef, 0x0a, 0x97, 0x2b, 0xdc, 0x59, 0x6a, 0x86, 0x61, 0x4e,
  0xa6, 0xc7, 0x95, 0x99, 0xa6, 0xa6, 0x55, 0x6c, 0x5a, 0x8e, 0x72, 0x25, 0x63, 0xac, 0x52, 0xb9,
  0x10, 0x69, 0x83, 0x99, 0xd3, 0x51, 0x6c, 0x1a, 0xb3, 0x83, 0x6a, 0xff, 0x50, 0x58, 0xb7, 0x28,
  0x97, 0x13, 0xe2, 0xba, 0x94, 0x5b, 0x89, 0xb4, 0xea, 0xba, 0x31, 0xcd, 0x78, 0xe4, 0x4a, 0x00,
  0x36, 0x42, 0x00, 0x62, 0x41, 0xc6, 0x47, 0x46, 0x37, 0xea, 0x6d, 0x50, 0xb4, 0x66, 0x8f, 0x55,
  0x0c, 0xc8, 0x99, 0x91, 0xd5, 0xec, 0xd2, 0x40, 0x1c, 0x24, 0x7d, 0x3a, 0xff, 0x74, 0xfa, 0x32,
  0x24, 0xe0, 0x11, 0x2b, 0x71, 0xad, 0x7e, 0x14, 0xa0, 0x77, 0x21, 0x68, 0x4f, 0xcc, 0xb6, 0x1b,
  0xe8, 0x00, 0x49, 0x13, 0x21, 0x02, 0x81, 0x81, 0x00, 0xb6, 0x18, 0x73, 0x59, 0x2c, 0x4f, 0x92,
  0xac, 0xa2, 0x2e, 0x5f, 0xb6, 0xbe, 0x78, 0x5d, 0x47, 0x71, 0x04, 0x92, 0xf0, 0xd7, 0xe8, 0xc5,
  0x7a, 0x84, 0x6b, 0xb8, 0xb4, 0x30, 0x1f, 0xd8, 0x0d, 0x58, 0xd0, 0x64, 0x80, 0xa7, 0x21, 0x1a,
  0x48, 0x00, 0x37, 0xd6, 0x19, 0x71, 0xbb, 0x91, 0x20, 0x9d, 0xe2, 0xc3, 0xec, 0xdb, 0x36, 0x1c,
  0xca, 0x48, 0x7d, 0x03, 0x32, 0x74, 0x1e, 0x65, 0x73, 0x02, 0x90, 0x73, 0xd8, 0x3f, 0xb5, 0x52,
  0x35, 0x79, 0x1c, 0xee, 0x93, 0xa3, 0x32, 0x8b, 0xed, 0x89, 0x98, 0xf1, 0x0c, 0xd8, 0x12, 0xf2,
  0x89, 0x7f, 0x32, 0x23, 0xec, 0x67, 0x66, 0x52, 0x83, 0x89, 0x99, 0x5e, 0x42, 0x2b, 0x42, 0x4b,
  0x84, 0x50, 0x1b, 0x3e, 0x47, 0x6d, 0x74, 0xfb, 0xd1, 0xa6, 0x10, 0x20, 0x6c, 0x6e, 0xbe, 0x44,
  0x3f, 0xb9, 0xfe, 0xbc, 0x8d, 0xda, 0xcb, 0xea, 0x8f
};

const char *db_name = "/tmp/ndnfs.db";
sqlite3 *db;

ndn::ptr_lib::shared_ptr<ndn::KeyChain> ndnfs::keyChain;
ndn::Name ndnfs::certificateName;
string ndnfs::global_prefix;
string ndnfs::root_path;

const int ndnfs::dir_type = 0;
const int ndnfs::file_type = 1;

const int ndnfs::seg_size = 8192;  // size of the content in each content object segment counted in bytes
const int ndnfs::seg_size_shift = 13;

int ndnfs::user_id = 0;
int ndnfs::group_id = 0;

static void create_fuse_operations(struct fuse_operations *fuse_op)
{
  fuse_op->getattr  = ndnfs_getattr;
  fuse_op->chmod    = ndnfs_chmod;
  fuse_op->setxattr = ndnfs_setxattr;
  fuse_op->open     = ndnfs_open;
  fuse_op->read     = ndnfs_read;
  fuse_op->readdir  = ndnfs_readdir;
  fuse_op->mknod    = ndnfs_mknod;
  fuse_op->write    = ndnfs_write;
  fuse_op->truncate = ndnfs_truncate;
  fuse_op->release  = ndnfs_release;
  fuse_op->unlink   = ndnfs_unlink;
  fuse_op->mkdir    = ndnfs_mkdir;
  fuse_op->rmdir    = ndnfs_rmdir;
  fuse_op->statfs   = ndnfs_statfs;
  fuse_op->access   = ndnfs_access;
}

static struct fuse_operations ndnfs_fs_ops;

struct ndnfs_config {
  char *prefix;
};

#define NDNFS_OPT(t, p, v) { t, offsetof(struct ndnfs_config, p), v }

static struct fuse_opt ndnfs_opts[] = {
  NDNFS_OPT("prefix=%s", prefix, 0),
  FUSE_OPT_END
};

void abs_path(char *dest, const char *path)
{
  strcpy(dest, ndnfs::root_path.c_str());
  strcat(dest, path);
}

void usage()
{
  cout << "Usage: ./ndnfs -s [actual folder directory (where files are stored in local file system)] [mount point directory] " << endl;
  return;
}

int main(int argc, char **argv)
{
  umask(0);
  
  // Initialize the keychain
  ndn::ptr_lib::shared_ptr<ndn::MemoryIdentityStorage> identityStorage(new ndn::MemoryIdentityStorage());
  ndn::ptr_lib::shared_ptr<ndn::MemoryPrivateKeyStorage> privateKeyStorage(new ndn::MemoryPrivateKeyStorage());
  ndnfs::keyChain.reset
	(new ndn::KeyChain
	  (ndn::ptr_lib::make_shared<ndn::IdentityManager>
		(identityStorage, privateKeyStorage), ndn::ptr_lib::shared_ptr<ndn::NoVerifyPolicyManager>
		  (new ndn::NoVerifyPolicyManager())));
  
  ndn::Name keyName("/testname/DSK-123");
  ndnfs::certificateName = keyName.getSubName(0, keyName.size() - 1).append("KEY").append
		 (keyName.get(keyName.size() - 1)).append("ID-CERT").append("0");
  identityStorage->addKey(keyName, ndn::KEY_TYPE_RSA, ndn::Blob(DEFAULT_RSA_PUBLIC_KEY_DER, sizeof(DEFAULT_RSA_PUBLIC_KEY_DER)));
  privateKeyStorage->setKeyPairForKeyName
	(keyName, ndn::KEY_TYPE_RSA, DEFAULT_RSA_PUBLIC_KEY_DER,
	 sizeof(DEFAULT_RSA_PUBLIC_KEY_DER), DEFAULT_RSA_PRIVATE_KEY_DER,
	 sizeof(DEFAULT_RSA_PRIVATE_KEY_DER));
  
  ndnfs::global_prefix = "/ndn/broadcast/ndnfs";
  
  cout << "main: NDNFS version 0.3" << endl;
  
  // Extract the root path (mount point) from running parameters;
  // it is not advised for fuse-mod to know mount point, as mount point may not be
  // static, and it may be cloned. Wonder what the common practice is for this.
  
  // First parameter not starting with '-' is the root path; 
  int i = 0;
  for(i = 1; (i < argc && argv[i][0] == '-'); i++);

  if (i == argc) {
	cerr << "main: missing actual folder path." << endl;
	usage();
	return -1;
  }
  
  // check if 'root_path' exists
  struct stat s;
  int err = stat(argv[i], &s);
  if (err == -1) {
	cerr << "main: actual folder does not exist." << endl;
	return 0;
  } else {
	if(S_ISDIR(s.st_mode)) {
	  cout << "main: root path is " << realpath(argv[i], NULL) << endl;
	} else {
      cerr << "main: actual folder is a file." << endl;
      return 0;
	}
  }
 
  ndnfs::root_path = string(realpath(argv[i], NULL));
  
  for(; i < argc; i++) {
	argv[i] = argv[i + 1];
  }
  argc--;
  
  if (ndnfs::root_path.back() == '/') {
	ndnfs::root_path = ndnfs::root_path.substr(0, ndnfs::root_path.size() - 1);
  }
   
  // uid and gid will be set to that of the user who starts the fuse process
  ndnfs::user_id = getuid();
  ndnfs::group_id = getgid();

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct ndnfs_config conf;
  memset(&conf, 0, sizeof(conf));
  fuse_opt_parse(&args, &conf, ndnfs_opts, NULL);

  if (conf.prefix != NULL) {
	ndn::Name InterestBaseName(conf.prefix);
	ndnfs::global_prefix = InterestBaseName.toUri();
  }
  cout << "main: global prefix is " << ndnfs::global_prefix << endl;

  cout << "main: test sqlite connection..." << endl;

  if (sqlite3_open(db_name, &db) == SQLITE_OK) {
	cout << "main: sqlite db open ok" << endl;
  } else {
	cout << "main: cannot connect to sqlite db, quit" << endl;
	sqlite3_close(db);
	return -1;
  }
    
  cout << "main: init tables in sqlite db..." << endl;
  
  // Init tables in database
  
  // Since actual data is not stored in database,
  // for each re-run, does it make sense to recreate the table no matter if it already exists.
  const char* INIT_FS_TABLE = "\
CREATE TABLE IF NOT EXISTS                        \n\
  file_system(                                    \n\
    path                 TEXT NOT NULL,           \n\
    parent               TEXT NOT NULL,           \n\
    type                 INTEGER,                 \n\
    mode                 INTEGER,                 \n\
    atime                INTEGER,                 \n\
    mtime                INTEGER,                 \n\
    size                 INTEGER,                 \n\
    current_version      INTEGER,                 \n\
    temp_version         INTEGER,                 \n\
    mime_type            TEXT,                    \n\
    ready_signed         INTEGER,                 \n\
    PRIMARY KEY (path)                            \n\
  );                                              \n\
CREATE INDEX id_path ON file_system (path);       \n\
CREATE INDEX id_parent ON file_system (parent);   \n\
";

  sqlite3_exec(db, INIT_FS_TABLE, NULL, NULL, NULL);

  const char* INIT_VER_TABLE = "\
CREATE TABLE IF NOT EXISTS                                   \n\
  file_versions(                                             \n\
    path          TEXT NOT NULL,                             \n\
    version       INTEGER,                                   \n\
    size          INTEGER,                                   \n\
    totalSegments INTEGER,                                   \n\
    PRIMARY KEY (path, version)                              \n\
  );                                                         \n\
CREATE INDEX id_ver ON file_versions (path, version);        \n\
";

  sqlite3_exec(db, INIT_VER_TABLE, NULL, NULL, NULL);

  const char* INIT_SEG_TABLE = "\
CREATE TABLE IF NOT EXISTS                                        \n\
  file_segments(                                                  \n\
    path        TEXT NOT NULL,                                    \n\
    version     INTEGER,                                          \n\
    segment     INTEGER,                                          \n\
    signature   BLOB NOT NULL,                                    \n\
    offset      INTEGER,                                          \n\
    PRIMARY KEY (path, version, segment)                          \n\
  );                                                              \n\
CREATE INDEX id_seg ON file_segments (path, version, segment);    \n\
";

  sqlite3_exec(db, INIT_SEG_TABLE, NULL, NULL, NULL);

  cout << "main: ok" << endl;

  cout << "main: initializing file mime_type inference..." << endl;
  initialize_ext_mime_map();

  cout << "main: mount root folder..." << endl;

  int now = time(0);
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, 
					 "INSERT INTO file_system (path, parent, type, mode, atime, mtime, size, current_version, temp_version) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);", 
					 -1, &stmt, 0);
  sqlite3_bind_text(stmt, 1, "/", -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, "", -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, ndnfs::dir_type);
  sqlite3_bind_int(stmt, 4, 0777);
  sqlite3_bind_int(stmt, 5, now);
  sqlite3_bind_int(stmt, 6, now);
  sqlite3_bind_int(stmt, 7, -1);  // size
  sqlite3_bind_int(stmt, 8, -1);  // current version
  sqlite3_bind_int(stmt, 9, -1);  // temp version
  int res = sqlite3_step(stmt);
  if (res == SQLITE_OK || res == SQLITE_DONE) {
	cout << "main: OK" << endl;
  }

  sqlite3_finalize(stmt);
  
  create_fuse_operations(&ndnfs_fs_ops);
  
  cout << "main: enter FUSE main loop" << endl << endl;
  return fuse_main(args.argc, args.argv, &ndnfs_fs_ops, NULL);
}
