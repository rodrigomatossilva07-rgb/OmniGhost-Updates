#include "kv3_parser.h"
#include <cassert>

int main() {
    const phys_extractor::Kv3Parser parser(R"KV3(<!-- kv3 metadata { ignored } -->
{
  // comments should not change strings
  m_parts = [ { name = "https://example.test/map" bytes = #[ 00 12 FF ] }, { name = "second" } ]
  m_count = 2
}
)KV3");
    assert(parser.get_value("m_parts[0].name") == "https://example.test/map");
    assert(parser.get_value("m_parts[0].bytes") == " 00 12 FF ");
    assert(parser.get_value("m_parts[1].name") == "second");
    assert(parser.get_value("m_parts[99].name").empty());
    assert(parser.get_value("m_parts[-1].name").empty());
    assert(parser.get_value("m_count") == "2");
    return 0;
}
