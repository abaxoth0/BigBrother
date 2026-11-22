import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_native_docs", ROOT / "scripts/check_native_docs.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NativeDocsTests(unittest.TestCase):
    def test_empty_xml_output_does_not_pass_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "no callable entries"):
                module.audit(Path(directory))

    @unittest.skipUnless(shutil.which("doxygen"), "Doxygen is required for extraction tests")
    def test_missing_private_and_anonymous_helpers_fail_but_header_contracts_count(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "fixture.h"
            source = root / "fixture.cpp"
            header.write_text('''/** @file fixture.h
 * @brief Fixture API.
 */
/** @brief Example resource owner. */
class Owner {
public:
    /** @brief Construct an empty owner. */
    Owner() = default;
    Owner(const Owner&) = delete;
private:
    void helper();
};
/** @brief Function documented at its declaration. */
void declared();
''')
            source.write_text('''#include "fixture.h"
void declared() {}
void Owner::helper() {}
namespace {
void hidden() {}
}
''')
            config = (ROOT / "Doxyfile").read_text() + f'''
INPUT = "{header}" "{source}"
OUTPUT_DIRECTORY = "{root / 'output'}"
EXTRACT_ALL = YES
EXTRACT_PRIVATE = YES
GENERATE_XML = YES
GENERATE_HTML = NO
'''

            def extract():
                subprocess.run(["doxygen", "-"], input=config, text=True, cwd=ROOT,
                               check=True, capture_output=True)
                return module.audit(root / "output/xml")

            count, missing = extract()
            self.assertGreaterEqual(count, 4)
            self.assertEqual(len(missing), 3, missing)
            self.assertTrue(any("file: fixture.cpp" in item for item in missing), missing)
            self.assertTrue(any("Owner::helper" in item for item in missing), missing)
            self.assertTrue(any("hidden" in item for item in missing), missing)

            header.write_text(header.read_text().replace(
                "    void helper();", "    /** @brief Release internal resources. */\n    void helper();"))
            source.write_text("/** @file fixture.cpp\n * @brief Fixture implementation.\n */\n" +
                              source.read_text().replace(
                                  "void hidden()", "/** @brief Internal operation. */\nvoid hidden()"))
            documented_count, missing = extract()
            self.assertEqual(documented_count, count)
            self.assertEqual(missing, [])
