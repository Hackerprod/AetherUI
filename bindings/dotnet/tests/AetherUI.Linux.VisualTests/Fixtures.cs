using AetherUI.Css;
using AetherUI.Primitives;
using AetherUI.Rendering;

namespace VisualValidation;

internal sealed record Fixture(string Id, PaintCommand[] Commands, Action<Raster, int> Check, bool NeedsFont = false);

internal static class Fixtures
{
    private static readonly ColorRgba Red = new(1, 0, 0);
    private static readonly ColorRgba Green = new(0, 1, 0);
    private static readonly ColorRgba Blue = new(0, 0, 1);
    private static readonly LinearGradient Gradient = new(1, 0, new[] { new GradientStop(Red, 0), new GradientStop(Blue, 1) });

    public static Fixture[] All(string fontFamily)
    {
        var fixtures = new List<Fixture>
        {
            new("solid-alpha", new PaintCommand[] {
                new FillRoundedRectangleCommand(new(16,16,80,64), default, new(1,0,0,.5f)),
                new FillRoundedRectangleCommand(new(48,32,80,64), default, new(0,0,1,.5f))
            }, (r,d) => {
                // 255*.5=127.5; source-over gives red=63.75, blue=127.5, alpha=191.25.
                // Allow two byte levels only here for input quantization and compositing rounding.
                Pixel(r,d,24,24,128,0,0,128,2); Pixel(r,d,64,48,64,0,128,192,2);
                Pixel(r,d,112,48,0,0,128,128,2); Pixel(r,d,8,8,0,0,0,0);
                Region(r,d,17,17,28,12,p=>Near(p.R,128,2)&&p.G==0&&p.B==0&&Near(p.A,128,2),"uniform red alpha interior");
            }),
            new("elliptic-radii", new PaintCommand[] {
                new FillRoundedRectangleCommand(new(24,24,160,112), new(40,16,12,32,32,24,20,40), Green)
            }, (r,d) => {
                Pixel(r,d,26,26,0,0,0,0); Pixel(r,d,64,26,0,255,0,255);
                Pixel(r,d,28,42,0,255,0,255); Pixel(r,d,180,26,0,0,0,0);
                Pixel(r,d,100,80,0,255,0,255); Pixel(r,d,25,132,0,0,0,0);
                Pixel(r,d,180,132,0,0,0,0); Pixel(r,d,190,80,0,0,0,0);
            }),
            BorderFixture(),
            new("stroke", new PaintCommand[] {
                new StrokeRoundedRectangleCommand(new(32,32,128,96), CornerRadii.Uniform(12), 8, Blue)
            }, (r,d) => {
                Pixel(r,d,96,32,0,0,255,255); Pixel(r,d,32,80,0,0,255,255);
                Pixel(r,d,96,80,0,0,0,0); Pixel(r,d,96,20,0,0,0,0);
            }),
            new("linear-gradient", new PaintCommand[] {
                new FillRoundedRectangleGradientCommand(new(24,24,160,80),default,new(24,24,160,80),Gradient,.75f)
            }, (r,d) => {
                var left=At(r,d,32,60); var mid=At(r,d,104,60); var right=At(r,d,176,60);
                Require(left.R>left.B+140 && right.B>right.R+140,"gradient direction and span");
                // Opacity .75 maps to 191.25. Three levels bound stop/pattern/pixel quantization.
                // Sample centers sit within .75 CSS px of the midpoint: channel difference <5.
                Require(Math.Abs(mid.R-mid.B)<5 && Near(mid.A,191,3),"gradient midpoint and opacity");
                Require(Near(left.A,191,3)&&Near(right.A,191,3),"gradient constant alpha");
                Pixel(r,d,20,60,0,0,0,0);
            }),
            new("clip-transform-stack",new PaintCommand[] {
                new PushTransformCommand(new(1,0,0,1,40,24)),
                new PushClipCommand(new(16,16,64,48),default),
                new FillRoundedRectangleCommand(new(0,0,128,96),default,Red),
                new PopClipCommand(),new PopTransformCommand(),
                new FillRoundedRectangleCommand(new(8,8,12,12),default,Blue)
            },(r,d)=> {
                Pixel(r,d,60,44,255,0,0,255); Pixel(r,d,112,80,255,0,0,255);
                Pixel(r,d,52,44,0,0,0,0); Pixel(r,d,124,44,0,0,0,0);
                Pixel(r,d,60,92,0,0,0,0); Pixel(r,d,12,12,0,0,255,255);
                Region(r,d,58,42,60,44,p=>p.R==255&&p.A==255,"translated clipped interior");
            }),
            ImageFixture(),
            new("box-shadow",new PaintCommand[] {
                new DrawBoxShadowCommand(new(48,40,80,64),CornerRadii.Uniform(8),new(10,8,8,2,new(0,0,0,.7f),false))
            },(r,d)=> {
                Pixel(r,d,80,72,0,0,0,0); Pixel(r,d,8,8,0,0,0,0);
                var near=At(r,d,134,72); var far=At(r,d,146,72);
                Require(near.A>40 && near.R==0&&near.G==0&&near.B==0,"shadow outside box with black premultiplication");
                Require(near.A>far.A+20,"shadow fades away from offset silhouette");
            }),
            new("backdrop-order",new PaintCommand[] {
                new FillRoundedRectangleCommand(new(16,16,144,112),default,ColorRgba.White),
                new ApplyBackdropFilterCommand(new(48,32,80,64),default,new[]{new BackdropFilter(BackdropFilterKind.Brightness,default,.5f)}),
                new FillRoundedRectangleCommand(new(80,56,16,16),default,Red)
            },(r,d)=> {
                // Brightness .5 maps white to 127.5; three levels allow filter quantization.
                Pixel(r,d,24,24,255,255,255,255); Pixel(r,d,64,48,128,128,128,255,3);
                Pixel(r,d,84,60,255,0,0,255); Pixel(r,d,180,80,0,0,0,0);
                Region(r,d,50,34,26,18,p=>Near(p.R,128,3)&&Near(p.G,128,3)&&Near(p.B,128,3)&&p.A==255,"backdrop filtered ROI");
            }),
            new("scrollbar-state",new PaintCommand[] {
                new PushClipCommand(new(16,16,144,112),default),new PushScrollOffsetCommand(7),
                new FillRoundedRectangleCommand(new(40,40,40,30),default,Red),
                new PopScrollOffsetCommand(),new PopClipCommand(),new DrawScrollbarChromeCommand(7)
            },(r,d)=> {
                Pixel(r,d,30,34,255,0,0,255); Pixel(r,d,70,48,0,0,0,0);
                var track=At(r,d,155,24); var thumb=At(r,d,155,48);
                Require(track.A>200&&thumb.A>200&&track.R>thumb.R+40,"vertical scrollbar track and thumb");
                Require(At(r,d,40,124).A>200&&At(r,d,155,124).A>200,"horizontal scrollbar and corner");
            })
        };
        foreach(var (id,text,size) in new[] {
            ("spanish-small","¡Hola, español!",12f),("combining-medium","Cafe\u0301 A\u0308 n\u0303",20f),
            ("rtl-medium","العربية עברית",24f),("cjk-medium","漢字 日本語",22f),
            ("emoji-large","😀 🌈",32f),("latin-large","Águila",36f) })
        {
            fixtures.Add(new("text-"+id,new PaintCommand[] {
                new DrawTextCommand(text,new(16,16,224,64),fontFamily,size,FontWeight.Normal,TextAlign.Start,ColorRgba.White)
            },(r,d)=> {
                Ink(r,d,16,16,224,64,20,"text ink in layout rectangle");
                Region(r,d,0,96,256,96,p=>p.A==0,"text does not escape layout rectangle");
            },true));
        }
        fixtures.Add(new("text-gradient-shadow",new PaintCommand[] {
            new DrawTextGradientCommand("GRADIENT",new(16,16,224,48),fontFamily,28,FontWeight.Normal,TextAlign.Start,Gradient,1),
            new DrawTextShadowCommand("SHADOW",new(16,96,220,48),fontFamily,28,FontWeight.Normal,TextAlign.Start,new(6,4,3,0,new(0,1,0,.8f),false))
        },(r,d)=> {
            Ink(r,d,16,16,224,48,60,"gradient glyph ink"); Ink(r,d,16,96,224,56,60,"shadow glyph ink");
            var red=Count(r,d,16,16,224,48,p=>p.R>p.B+20&&p.A>30);
            var blue=Count(r,d,16,16,224,48,p=>p.B>p.R+20&&p.A>30);
            Require(red>10*d*d&&blue>10*d*d,"gradient color variation across glyphs");
            Region(r,d,16,96,224,56,p=>p.R==0&&p.B==0&&p.G<=p.A,"uniformly green premultiplied text shadow");
            Pixel(r,d,4,180,0,0,0,0);
        },true));
        var concrete=typeof(PaintCommand).Assembly.GetTypes().Where(t=>!t.IsAbstract&&typeof(PaintCommand).IsAssignableFrom(t)).ToHashSet();
        var covered=fixtures.SelectMany(f=>f.Commands).Select(c=>c.GetType()).ToHashSet();
        Require(concrete.SetEquals(covered),"PaintCommand fixture inventory mismatch: "+string.Join(", ",concrete.Except(covered).Select(t=>t.Name)));
        // Bounds are declared from fixture geometry, never inferred from rendered pixels.
        // Every pixel outside their union must remain exactly transparent.
        return fixtures.Select(f => {
            var bounds = ExteriorBounds(f.Id);
            return f with { Check = (r,d) => {
                Require(r.Width==256*d&&r.Height==192*d,"fixture viewport dimensions");
                Exterior(r,d,bounds);
                if(f.NeedsFont) {
                    // Coarse anti-solid-box guard, not a shaping/glyph-coverage oracle.
                    // Each independent text/effect ROI must have at most 50% pixels with alpha>16.
                    foreach(var roi in bounds)
                        Require(Count(r,d,roi.X,roi.Y,roi.Width,roi.Height,p=>p.A>16)
                            <= roi.Width*roi.Height*d*d/2,"text ROI coverage exceeds 50%");
                }
                f.Check(r,d);
            }};
        }).ToArray();
    }

    private readonly record struct Roi(int X,int Y,int Width,int Height);
    private static Roi[] ExteriorBounds(string id) => id switch {
        "solid-alpha" => new[]{new Roi(16,16,80,64),new Roi(48,32,80,64)},
        "elliptic-radii" or "unequal-border-sides" => new[]{new Roi(24,24,160,112)},
        "stroke" => new[]{new Roi(28,28,136,104)}, // centered 8px stroke extends 4px
        "linear-gradient" => new[]{new Roi(24,24,160,80)},
        "clip-transform-stack" => new[]{new Roi(56,40,64,48),new Roi(8,8,12,12)},
        "embedded-image" => new[]{new Roi(24,24,128,96)},
        // Offset/spread silhouette is [56,46,140,114]. Blur sigma=4, radius<=12,
        // plus two CSS pixels for raster/filter padding: [42,32,154,128].
        "box-shadow" => new[]{new Roi(42,32,112,96)},
        "backdrop-order" => new[]{new Roi(16,16,144,112)},
        "scrollbar-state" => new[]{new Roi(28,32,40,30),new Roi(152,16,8,112),new Roi(16,120,136,8)},
        // Shadow layout after offset: [22,100,242,148]; sigma=1.5, ceil(3*sigma)=5,
        // plus two CSS pixels yields [15,93,249,155]. Gradient has its own clipped ROI.
        "text-gradient-shadow" => new[]{new Roi(16,16,224,48),new Roi(15,93,234,62)},
        "text-spanish-small" or "text-combining-medium" or "text-rtl-medium" or
        "text-cjk-medium" or "text-emoji-large" or "text-latin-large" => new[]{new Roi(16,16,224,64)},
        _ => throw new InvalidOperationException("Missing declared exterior mask: "+id)
    };
    private static void Exterior(Raster r,int d,Roi[] bounds) {
        for(var y=0;y<r.Height;y++) for(var x=0;x<r.Width;x++) {
            if(bounds.Any(b=>x>=b.X*d&&x<(b.X+b.Width)*d&&y>=b.Y*d&&y<(b.Y+b.Height)*d)) continue;
            Require(r.Pixels[(y*r.Width+x)*4+3]==0,$"nontransparent exterior pixel ({x},{y}) at DPI {d}");
        }
    }

    private static Fixture BorderFixture()
    {
        var colors=new[]{Red,Green,Blue,ColorRgba.White};
        return new("unequal-border-sides",Enumerable.Range(0,4).Select(side=>(PaintCommand)new FillBorderSideCommand(
            new(24,24,160,112),default,new(34,30,130,92),default,side,(int)BorderStyle.Solid,colors[side])).ToArray(),(r,d)=> {
            Pixel(r,d,96,26,255,0,0,255); Pixel(r,d,174,80,0,255,0,255);
            Pixel(r,d,96,130,0,0,255,255); Pixel(r,d,28,80,255,255,255,255);
            Pixel(r,d,96,80,0,0,0,0); Pixel(r,d,20,80,0,0,0,0);
        });
    }

    private static Fixture ImageFixture()
    {
        var bytes=new byte[8*8*4];
        for(var y=0;y<8;y++) for(var x=0;x<8;x++) {
            var i=(y*8+x)*4; bytes[i]=(byte)(y>=4?255:0); bytes[i+1]=(byte)(x>=4?255:0);
            bytes[i+2]=(byte)(y<4&&x<4?255:0); bytes[i+3]=255;
        }
        return new("embedded-image",new PaintCommand[]{new DrawBackgroundImageCommand("embedded-fixture",new(24,24,128,96),default,default,
            BackgroundImageSizeKind.Auto,default,default,BackgroundRepeat.NoRepeat,BackgroundRepeat.NoRepeat,1,
            new(0,0,8,8),new(24,24,128,96),true,new(new(8,8),bytes))},(r,d)=> {
                Pixel(r,d,40,40,255,0,0,255); Pixel(r,d,136,40,0,255,0,255);
                Pixel(r,d,40,104,0,0,255,255); Pixel(r,d,136,104,0,255,255,255);
                Pixel(r,d,20,40,0,0,0,0); Pixel(r,d,160,104,0,0,0,0);
            });
    }

    private readonly record struct Sample(int R,int G,int B,int A);
    private static Sample At(Raster r,int dpi,int x,int y) => Physical(r,x*dpi+dpi/2,y*dpi+dpi/2);
    private static Sample Physical(Raster r,int x,int y) {
        Require(x>=0&&y>=0&&x<r.Width&&y<r.Height,"sample inside raster");
        var i=(y*r.Width+x)*4; return new(r.Pixels[i+2],r.Pixels[i+1],r.Pixels[i],r.Pixels[i+3]);
    }
    private static bool Near(int a,int b,int tolerance=0)=>Math.Abs(a-b)<=tolerance;
    private static void Pixel(Raster r,int d,int x,int y,int red,int green,int blue,int alpha,int tolerance=0) {
        var p=At(r,d,x,y); Require(Near(p.R,red,tolerance)&&Near(p.G,green,tolerance)&&Near(p.B,blue,tolerance)&&Near(p.A,alpha,tolerance),
            $"pixel ({x},{y}) at DPI {d}: expected RGBA({red},{green},{blue},{alpha}), got {p}");
    }
    private static int Count(Raster r,int d,int x,int y,int w,int h,Func<Sample,bool> predicate) {
        var count=0; for(var yy=y*d;yy<(y+h)*d;yy++) for(var xx=x*d;xx<(x+w)*d;xx++) if(predicate(Physical(r,xx,yy))) count++;
        return count;
    }
    private static void Region(Raster r,int d,int x,int y,int w,int h,Func<Sample,bool> predicate,string name) =>
        Require(Count(r,d,x,y,w,h,predicate)==w*h*d*d,name);
    private static void Ink(Raster r,int d,int x,int y,int w,int h,int min,string name) =>
        Require(Count(r,d,x,y,w,h,p=>p.A>16)>=min*d*d,name);
    private static void Require(bool value,string message) { if(!value) throw new InvalidOperationException(message); }
}
