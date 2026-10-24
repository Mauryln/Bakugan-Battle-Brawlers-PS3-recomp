from pathlib import Path
import difflib,tempfile,unittest
import pipeline

class PatchSeriesTest(unittest.TestCase):
    def test_overlapping_prefixes_preserve_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); checkout=root/'toolkit'; checkout.mkdir()
            versions=['first\n','second\n','third\n','fourth\n']
            patches=[]
            for i in range(3):
                patch=root/f'{i+1:04}.patch'
                patch.write_text(''.join(difflib.unified_diff(versions[i].splitlines(True),versions[i+1].splitlines(True),fromfile='a/file.txt',tofile='b/file.txt')),encoding='utf-8')
                patches.append(patch)
            for count, content in enumerate(versions):
                with self.subTest(applied=count):
                    target=checkout/'file.txt'; target.write_text(content,encoding='utf-8')
                    self.assertEqual(pipeline.applied_patch_count(checkout,patches),count)
                    self.assertEqual(target.read_text(encoding='utf-8'),content)

if __name__=='__main__':
    unittest.main()
