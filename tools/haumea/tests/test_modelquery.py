# SPDX-License-Identifier: GPL-2.0-or-later
"""Check bounded multi-ID parsing and size/proportion comparisons."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ModelQueryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'needs a C++ compiler')
    def test_queries_and_dimensions(self):
        src = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='haumea-modelquery-') as tmp:
            test = Path(tmp, 'check.cpp')
            test.write_text(r'''#include "modelquery.h"
#include <cassert>
#include <cmath>
#include <climits>
#include <limits>
int main() {
    std::vector<std::pair<int, int>> ranges;
    assert(ParseModelIdQuery("1337, 19355-19353; 1271 1280", &ranges));
    assert(ranges.size() == 4);
    assert(ranges[1] == std::make_pair(19353, 19355));
    assert(ranges[2] == std::make_pair(1271, 1271));
    assert(ParseModelIdQuery("-1000,-3000--2000", &ranges));
    assert(ranges.size() == 2 && ranges[1] == std::make_pair(-3000,-2000));
    assert(ParseModelIdQuery("-2147483648-2147483647", &ranges));
    assert(ranges.size() == 1 && ranges[0] == std::make_pair(INT_MIN, INT_MAX));
    for(const char *bad : {"", " ", "x", "1337frog", "1-", "1.5", "9999999999999999999999", "2147483648", "-2147483649"})
        assert(!ParseModelIdQuery(bad, &ranges));
    assert(!ParseModelIdQuery(nullptr, &ranges));
    assert(ModelDimensionDifference({2,4,3}, {4,2,3}, false) == 0);
    assert(ModelDimensionDifference({2,4,3}, {20,40,30}, true) < 0.001f);
    assert(ModelDimensionDifference({2,4,3}, {20,40,30}, false) > 1);
    assert(ModelDimensionDifference({2,4,3}, {2,4,6}, true) > .3f);
    assert(ModelDimensionDifference({2,4,3}, {2.01f,4.01f,3.01f}, false) == 0);
    assert(std::isinf(ModelDimensionDifference({0,0,0}, {2,4,3}, true)));
    assert(std::isinf(ModelDimensionDifference({2,4,3}, {0,0,0}, false)));
}''')
            binary = Path(tmp, 'check')
            subprocess.run(['c++', '-std=c++17', '-I', str(src), str(test),
                            str(src / 'modelquery.cpp'), '-o', str(binary)], check=True,
                           capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
