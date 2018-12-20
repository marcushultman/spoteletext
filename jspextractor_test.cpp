#include "jspextractor.h"

void testjson() {
  jsproperty::extractor foo("foo");
  char *chunk1 = R"json({ "bar": 1, "nested": {
    "burn": 2
  },
  "foo": "HE)json";
  char *chunk2 = R"json(JHEJ"})json";
  foo.feed(chunk1, strlen(chunk1));
  foo.feed(chunk2, strlen(chunk2));
  // foo.feed(R"json({"foo":42})json");
  std::cerr << "FOOBAR" << foo.extracted() << ": " << *foo << std::endl;
}
