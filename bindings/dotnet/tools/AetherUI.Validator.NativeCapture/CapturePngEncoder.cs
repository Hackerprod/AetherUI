using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;
namespace AetherUI.Validator.NativeCapture;
internal static class CapturePngEncoder
{
    public static byte[] ToRgba(byte[] bgra,int width,int height)
    {
        if (width<=0 || height<=0 || width>1440 || height>900 || bgra.Length!=checked(width*height*4)) throw new ArgumentException("Invalid packed BGRA dimensions or length");
        byte[] rgba=new byte[bgra.Length];
        for(int i=0;i<bgra.Length;i+=4)
        {
            int a=bgra[i+3]; rgba[i+3]=(byte)a;
            for(int c=0;c<3;c++) rgba[i+c]=a==0?(byte)0:(byte)Math.Min(255,(bgra[i+2-c]*255+a/2)/a);
        }
        return rgba;
    }
    public static void Write(string path,byte[] bgra,int width,int height)
    {
        byte[] rgba=ToRgba(bgra,width,height);
        using var output=new FileStream(path,FileMode.CreateNew,FileAccess.Write);
        output.Write(new byte[]{137,80,78,71,13,10,26,10});
        byte[] header=new byte[13]; BinaryPrimitives.WriteInt32BigEndian(header,width); BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4),height); header[8]=8; header[9]=6;
        Chunk(output,"IHDR",header);
        using var compressed=new MemoryStream();
        using(var zlib=new ZLibStream(compressed,CompressionLevel.Optimal,true))
            for(int y=0;y<height;y++) { zlib.WriteByte(0); zlib.Write(rgba,y*width*4,width*4); }
        Chunk(output,"IDAT",compressed.ToArray()); Chunk(output,"IEND",[]);
    }
    public static void Validate(string path,byte[] bgra,int width,int height)
    {
        byte[] expected=ToRgba(bgra,width,height);
        using var decoded=new System.Drawing.Bitmap(path);
        if(decoded.Width!=width || decoded.Height!=height) throw new InvalidDataException("PNG dimensions differ");
        for(int y=0;y<height;y++) for(int x=0;x<width;x++)
        {
            var actual=decoded.GetPixel(x,y); int i=(y*width+x)*4;
            if(actual.R!=expected[i] || actual.G!=expected[i+1] || actual.B!=expected[i+2] || actual.A!=expected[i+3]) throw new InvalidDataException("Independent PNG roundtrip differs");
        }
    }
    private static void Chunk(Stream output,string name,byte[] data)
    {
        Span<byte> number=stackalloc byte[4]; BinaryPrimitives.WriteInt32BigEndian(number,data.Length); output.Write(number);
        byte[] type=Encoding.ASCII.GetBytes(name); output.Write(type); output.Write(data);
        uint crc=0xffffffff;
        foreach(byte b in type.Concat(data)) { crc^=b; for(int i=0;i<8;i++) crc=(crc&1)!=0?0xedb88320^(crc>>1):crc>>1; }
        BinaryPrimitives.WriteUInt32BigEndian(number,crc^0xffffffff); output.Write(number);
    }
}
