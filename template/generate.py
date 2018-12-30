#!/usr/bin/env python3

def generate():
  out = open("../sptemplates.h", "w")
  out.write(
"""/* Auto-generated code. Do not change. */

#pragma once

namespace teletext {
namespace templates {

""")
  lines = open("npv.tti").readlines()
  context, image, title, artist = (-2, 0, -2, -2)
  for line in lines[0:6]:
    context += len(line)
  for line in lines[0:7]:
    image += len(line)
  for line in lines[0:9]:
    title += len(line)
  for line in lines[0:11]:
    artist += len(line)
  out.write("const int kNpvContextOffset = " + str(context) + ";\n")
  out.write("const int kNpvImageOffset = " + str(image) + ";\n")
  out.write("const int kNpvTitleOffset = " + str(title) + ";\n")
  out.write("const int kNpvArtistOffset = " + str(artist) + ";\n\n")

  out.write("const int kNpvImageLineBegin = 4;\n")
  out.write("const int kNpvImageLineEnd = 18;\n\n")

  out.write("const char kNpv[] = {\n")
  out.write("\n".join(map(lambda line : "    " + ", ".join(map(lambda c : hex(ord(c)), list(line))) + ",", lines)))
  out.write("\n};\n\n")

  lines = open("pair.tti").readlines()
  url, code = (-1, -1)
  for line in lines[0:14]:
    url += len(line)
  for line in lines[0:17]:
    code += len(line)
  out.write("const int kPairCodeOffset = " + str(code) + ";\n")
  out.write("const int kPairUrlOffset = " + str(url) + ";\n\n")
  out.write("const char kPair[] = {\n")
  out.write("\n".join(map(lambda line : "    " + ", ".join(map(lambda c : hex(ord(c)), list(line))) + ",", lines)))
  out.write("\n};\n\n")

  out.write(
"""
}  // namespace templates
}  // namespace teletext
""")
  print("done!")

if __name__ == "__main__":
  generate()
