import pathlib, tempfile, unittest
from compare import compare_files

class ComparisonSensitivity(unittest.TestCase):
    def test_copied_output_differences(self):
        original=b'dom[0].style.width.value=f:0\ndom[0].style.font_family=s:6162\ndom[0].style.box_shadows.length=i:1\ndom[0].style.box_shadows[0].color.r=f:0\ndisplay[0].tag=i:4\ndisplay[0].data.draw_text.text=s:78\n'
        variants=[original.replace(b'f:0',b'f:1',1), original.replace(b's:6162',b's:6163'),original.replace(b'length=i:1',b'length=i:0'),original.replace(b'color.r=f:0',b'color.r=f:1'),original.replace(b'tag=i:4',b'tag=i:5'),original.replace(b'text=s:78',b'text=null'),b''.join(original.splitlines(keepends=True)[::-1]),original+b'extra=i:1\n',original.rsplit(b'\n',2)[0]+b'\n']
        with tempfile.TemporaryDirectory() as d:
            a,b=pathlib.Path(d)/'a',pathlib.Path(d)/'b';a.write_bytes(original);b.write_bytes(original);compare_files(a,b)
            for i,v in enumerate(variants):
                with self.subTest(variant=i):
                    b.write_bytes(v)
                    with self.assertRaises(AssertionError):compare_files(a,b)
if __name__=='__main__':unittest.main()
