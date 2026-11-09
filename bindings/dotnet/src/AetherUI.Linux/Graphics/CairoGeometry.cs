using System.Numerics;
using AetherUI.Css;
using AetherUI.Primitives;
using AetherUI.Rendering;
using static AetherUI.Linux.Graphics.CairoNative;
namespace AetherUI.Linux.Graphics;
public sealed partial class CairoRenderBackend
{
    private static void Finite(params double[] values) { if(values.Any(x=>!double.IsFinite(x))) throw new ArgumentException("Drawing coordinates must be finite."); }
    private static void Color(IntPtr cr,ColorRgba c) { Finite(c.R,c.G,c.B,c.A);cairo_set_source_rgba(cr,Math.Clamp(c.R,0,1),Math.Clamp(c.G,0,1),Math.Clamp(c.B,0,1),Math.Clamp(c.A,0,1)); }
    private static void RoundPath(IntPtr cr,RectF r,CornerRadii a)
    {
        Finite(r.X,r.Y,r.Width,r.Height,a.TopLeftX,a.TopLeftY,a.TopRightX,a.TopRightY,a.BottomLeftX,a.BottomLeftY,a.BottomRightX,a.BottomRightY);
        cairo_new_path(cr);AppendRoundPath(cr,r,a);
    }
    private static void AppendRoundPath(IntPtr cr,RectF r,CornerRadii a)
    {
        if(r.Width<=0||r.Height<=0) return;
        var v=new[]{a.TopLeftX,a.TopLeftY,a.TopRightX,a.TopRightY,a.BottomRightX,a.BottomRightY,a.BottomLeftX,a.BottomLeftY}.Select(x=>Math.Max(0,(double)x)).ToArray();
        var s=Math.Min(1,Math.Min(Math.Min(r.Width/Math.Max(.0001,v[0]+v[2]),r.Width/Math.Max(.0001,v[4]+v[6])),Math.Min(r.Height/Math.Max(.0001,v[1]+v[7]),r.Height/Math.Max(.0001,v[3]+v[5]))));
        for(int i=0;i<8;i++) v[i]*=s;
        const double k=.5522847498307936;
        cairo_move_to(cr,r.Left+v[0],r.Top);cairo_line_to(cr,r.Right-v[2],r.Top);
        cairo_curve_to(cr,r.Right-v[2]*(1-k),r.Top,r.Right,r.Top+v[3]*(1-k),r.Right,r.Top+v[3]);
        cairo_line_to(cr,r.Right,r.Bottom-v[5]);cairo_curve_to(cr,r.Right,r.Bottom-v[5]*(1-k),r.Right-v[4]*(1-k),r.Bottom,r.Right-v[4],r.Bottom);
        cairo_line_to(cr,r.Left+v[6],r.Bottom);cairo_curve_to(cr,r.Left+v[6]*(1-k),r.Bottom,r.Left,r.Bottom-v[7]*(1-k),r.Left,r.Bottom-v[7]);
        cairo_line_to(cr,r.Left,r.Top+v[1]);cairo_curve_to(cr,r.Left,r.Top+v[1]*(1-k),r.Left+v[0]*(1-k),r.Top,r.Left+v[0],r.Top);cairo_close_path(cr);
    }
    private static void Gradient(IntPtr cr,RectF rect,LinearGradient gradient,float opacity)
    {
        Finite(rect.X,rect.Y,rect.Width,rect.Height,gradient.DirectionX,gradient.DirectionY,opacity);
        if(gradient.Stops is null||gradient.Stops.Count==0) throw new ArgumentException("Gradient requires stops.");
        var dx=gradient.DirectionX;var dy=gradient.DirectionY;var length=MathF.Sqrt(dx*dx+dy*dy);if(length<=0) {dx=0;dy=1;}else{dx/=length;dy/=length;}
        var extent=(Math.Abs(rect.Width*dx)+Math.Abs(rect.Height*dy))/2;var x=rect.X+rect.Width/2;var y=rect.Y+rect.Height/2;
        var pattern=cairo_pattern_create_linear(x-dx*extent,y-dy*extent,x+dx*extent,y+dy*extent);
        try { foreach(var stop in gradient.Stops) { var c=stop.Color;Finite(stop.Offset,c.R,c.G,c.B,c.A);cairo_pattern_add_color_stop_rgba(pattern,Math.Clamp(stop.Offset,0,1),Math.Clamp(c.R,0,1),Math.Clamp(c.G,0,1),Math.Clamp(c.B,0,1),Math.Clamp(c.A*opacity,0,1)); }cairo_pattern_set_extend(pattern,3);Check(cairo_pattern_status(pattern));cairo_set_source(cr,pattern); }
        finally { cairo_pattern_destroy(pattern); }
    }
    private static void DrawImage(IntPtr cr,DrawBackgroundImageCommand c)
    {
        if(c.Resource is null) return;
        var image=c.Resource;var w=image.Dimensions.Width;var h=image.Dimensions.Height;
        ValidateViewport(w,h,1);if(w==0||h==0||image.Pixels.Length!=checked(w*h*4)) throw new ArgumentException("Invalid premultiplied BGRA image.");
        var d=c.DestinationRectangle;var s=c.SourceRectangle;
        Finite(d.X,d.Y,d.Width,d.Height,s.X,s.Y,s.Width,s.Height,c.Opacity);
        if(!c.HasResolvedGeometry||d.IsEmpty||s.IsEmpty||c.Opacity<=0) return;
        using var surface=new CairoSurface(w,h);surface.WritePixels(image.Pixels.ToArray());
        cairo_save(cr);try { cairo_rectangle(cr,d.X,d.Y,d.Width,d.Height);cairo_clip(cr);cairo_translate(cr,d.X,d.Y);cairo_scale(cr,d.Width/s.Width,d.Height/s.Height);cairo_set_source_surface(cr,surface.Handle,-s.X,-s.Y);cairo_paint_with_alpha(cr,Math.Clamp(c.Opacity,0,1)); }finally {cairo_restore(cr);}
    }
    private static void PaintPixels(IntPtr cr,byte[] bytes,int width,int height)
    {
        using var surface=new CairoSurface(width,height);surface.WritePixels(bytes);cairo_save(cr);try {cairo_identity_matrix(cr);cairo_set_source_surface(cr,surface.Handle,0,0);cairo_paint(cr);}finally{cairo_restore(cr);}
    }
    private static void FillRect(IntPtr cr,RectF r,ColorRgba color) {RoundPath(cr,r,default);Color(cr,color);cairo_fill(cr);}
    private static void DrawChrome(IntPtr cr,ScrollbarChrome c)
    {
        var track=new ColorRgba(.94f,.94f,.94f,.94f);var thumb=new ColorRgba(.55f,.55f,.55f,.85f);
        if(c.HorizontalVisible){FillRect(cr,c.HorizontalTrack,track);FillRect(cr,c.HorizontalThumb,thumb);}
        if(c.VerticalVisible){FillRect(cr,c.VerticalTrack,track);FillRect(cr,c.VerticalThumb,thumb);}
        if(c.HorizontalVisible&&c.VerticalVisible)FillRect(cr,c.Corner,track);
    }
    private static void DrawBorder(IntPtr cr,FillBorderSideCommand c)
    {
        if(c.Side<0||c.Side>3||!Enum.IsDefined((BorderStyle)c.Style)) throw new ArgumentException("Invalid border side/style.");
        cairo_save(cr);try
        {
            RoundPath(cr,c.OuterRectangle,c.OuterRadii);AppendRoundPath(cr,c.InnerRectangle,c.InnerRadii);cairo_set_fill_rule(cr,1);cairo_clip(cr);cairo_set_fill_rule(cr,0);
            var o=c.OuterRectangle;var i=c.InnerRectangle;
            var outer=new[]{new Vector2(o.Left,o.Top),new Vector2(o.Right,o.Top),new Vector2(o.Right,o.Bottom),new Vector2(o.Left,o.Bottom)};
            var inner=new[]{new Vector2(i.Left,i.Top),new Vector2(i.Right,i.Top),new Vector2(i.Right,i.Bottom),new Vector2(i.Left,i.Bottom)};
            var points=ClipHalf(ClipHalf(outer.ToList(),outer[c.Side],inner[c.Side],true),outer[(c.Side+1)%4],inner[(c.Side+1)%4],false);
            cairo_new_path(cr);if(points.Count>0){cairo_move_to(cr,points[0].X,points[0].Y);foreach(var p in points.Skip(1))cairo_line_to(cr,p.X,p.Y);cairo_close_path(cr);}cairo_clip(cr);
            var horizontal=c.Side is 0 or 2;var t=c.Side switch{0=>i.Top-o.Top,1=>o.Right-i.Right,2=>o.Bottom-i.Bottom,_=>i.Left-o.Left};t=Math.Abs(t);
            var style=(BorderStyle)c.Style;var col=c.Color;
            ColorRgba Shade(float f)=>new(Math.Clamp(col.R*f,0,1),Math.Clamp(col.G*f,0,1),Math.Clamp(col.B*f,0,1),col.A);
            if(style is BorderStyle.None or BorderStyle.Hidden) return;
            if(style is BorderStyle.Inset or BorderStyle.Outset) {FillRect(cr,o,Shade((style==BorderStyle.Inset)==(c.Side is 0 or 3)?.65f:1.35f));return;}
            if(style is BorderStyle.Groove or BorderStyle.Ridge && t>=2)
            {
                var first=t/2;var dark=style==BorderStyle.Groove?c.Side is 0 or 3:c.Side is 1 or 2;
                FillRect(cr,horizontal?new(o.X,o.Y,o.Width,first):new(o.X,o.Y,first,o.Height),Shade(dark?.65f:1.35f));
                FillRect(cr,horizontal?new(o.X,o.Y+first,o.Width,o.Height-first):new(o.X+first,o.Y,o.Width-first,o.Height),Shade(dark?1.35f:.65f));return;
            }
            if(style==BorderStyle.Double&&t>=3){var b=t/3;FillRect(cr,horizontal?new(o.X,o.Y,o.Width,b):new(o.X,o.Y,b,o.Height),col);FillRect(cr,horizontal?new(o.X,o.Bottom-b,o.Width,b):new(o.Right-b,o.Y,b,o.Height),col);return;}
            if(style is BorderStyle.Dotted or BorderStyle.Dashed){var len=horizontal?o.Width:o.Height;var seg=style==BorderStyle.Dotted?t:Math.Max(3*t,3);var step=seg+Math.Max(t,1);for(float p=0;p<len;p+=step)FillRect(cr,horizontal?new(o.X+p,o.Y,Math.Min(seg,len-p),o.Height):new(o.X,o.Y+p,o.Width,Math.Min(seg,len-p)),col);return;}
            FillRect(cr,o,col);
        }
        finally{cairo_restore(cr);}
    }
    private static List<Vector2> ClipHalf(List<Vector2> points,Vector2 start,Vector2 end,bool negative)
    {
        var line=end-start;if(line.LengthSquared()<1e-8)return points;
        float Side(Vector2 p)=>line.X*(p.Y-start.Y)-line.Y*(p.X-start.X);
        bool Keep(float s)=>negative?s<=.0001f:s>=-.0001f;
        var result=new List<Vector2>();for(var n=0;n<points.Count;n++){var a=points[n];var b=points[(n+1)%points.Count];var sa=Side(a);var sb=Side(b);var ka=Keep(sa);var kb=Keep(sb);if(ka!=kb)result.Add(a+(b-a)*(sa/(sa-sb)));if(kb)result.Add(b);}return result;
    }
}
