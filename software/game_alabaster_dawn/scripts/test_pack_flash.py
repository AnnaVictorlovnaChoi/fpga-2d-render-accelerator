"""Synthetic host regression; never creates production delivery images."""
import tempfile, unittest, struct, zlib
from pathlib import Path
from pack_flash import *
from bin_to_hex import convert

def fix(b):
    b=bytearray(b);n=struct.unpack_from('<I',b,20)[0];po=struct.unpack_from('<Q',b,32)[0]
    struct.pack_into('<I',b,48,crc(b[64:64+n*64]));struct.pack_into('<I',b,52,crc(b[po:]));struct.pack_into('<I',b,56,0);struct.pack_into('<I',b,56,crc(b[:64]));return bytes(b)
class PackTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.addCleanup(self.tmp.cleanup)
        raw=b'predictor bytes\x00'*80;c=zlib.compressobj(9,zlib.DEFLATED,-15);data=c.compress(raw)+c.flush();(self.root/'a').write_bytes(data)
        self.man={'entries':[dict(kind=1,id=i,codec=1,predictor=1,path='a',raw_bytes=len(raw)) for i in range(2)]};self.pack=build(self.man,self.root)
    def test_valid_consumer_groups(self):
        self.assertEqual(len(validate(self.pack,self.man['entries'])),2)
        with self.assertRaisesRegex(ValueError,'consumer'):validate(self.pack,[dict(kind=2,id=0,codec=1,raw_bytes=1200)])
    def test_bad_header_crcs(self):
        for off in (0,48,52,56,128):
            b=bytearray(self.pack);b[off]^=1
            with self.assertRaises(ValueError):validate(b)
    def test_overflow_overlap_rawcrc(self):
        for off,value,fmt in [(80,2**64-8,'Q'),(80,struct.unpack_from('<Q',self.pack,144)[0],'Q'),(112,0,'I')]:
            b=bytearray(self.pack);struct.pack_into('<'+fmt,b,off,value)
            with self.assertRaises(ValueError):validate(fix(b))
    def test_full_hex_exact(self):
        app=b'app'+bytes(range(256));full=full_image(app,self.pack);p=self.root/'full';h=self.root/'hex';p.write_bytes(full);convert(p,h,FLASH_LIMIT)
        lines=h.read_bytes().splitlines();self.assertTrue(all(len(x)==2 for x in lines));back=bytes(int(x,16) for x in lines)
        self.assertEqual(back[:len(app)],app);self.assertEqual(back[len(app):APP_LIMIT],b'\xff'*(APP_LIMIT-len(app)));self.assertEqual(back[APP_LIMIT:],self.pack)
    def test_16m_boundary_and_end_sentinel(self):
        # Stored synthetic stream crosses absolute Flash 16MiB; final byte sentinel preserved.
        data=b'Z'*(14<<20)+b'END!';(self.root/'large').write_bytes(data)
        p=build({'entries':[dict(kind=4,id=0,codec=0,path='large',raw_bytes=len(data))]},self.root)
        full=full_image(b'A',p);self.assertGreater(len(full),16<<20);self.assertEqual(full[-4:],b'END!');validate(p)
    def test_trailing_deflate_rejected(self):
        with self.assertRaises(ValueError):decoded((self.root/'a').read_bytes()+b'bad',1)
if __name__=='__main__':unittest.main()
