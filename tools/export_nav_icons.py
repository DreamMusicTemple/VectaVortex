#!/usr/bin/env python3
"""Navigation / section icons. Single source of truth for the native ports of the web app's icon-only tab bar (the web uses the same art as an inline SVG sprite).
Writes Juce/Source/NavIcons.h (juce::Graphics drawing code) and Apple/Shared/NavIcons.swift (SwiftUI Path + UIImage/NSImage renderer).
All icons live on a 24x24 grid: 1.6 stroke, round caps and joins, `currentColor` (= the colour passed at draw time). Usage: python3 tools/export_nav_icons.py"""
import re, os, math
import xml.etree.ElementTree as ET
import math
def gear():
    n=8; ro=9.6; rr=7.0; pts=[]
    for i in range(n):
        a=2*math.pi*i/n
        for da,r in ((-0.30,rr),(-0.17,ro),(0.17,ro),(0.30,rr)):
            t=a+da; pts.append((12+r*math.sin(t),12-r*math.cos(t)))
    return 'M'+'L'.join('%.1f %.1f'%p for p in pts)+'Z'
GEAR=gear()
# each icon: inner svg markup on a 24x24 grid. Stroke styling comes from the .vi class (currentColor, 1.6, round).
ICONS={
 'osc':'<path d="M2 5q2.5-4.5 5 0t5 0t5 0t5 0"/><path d="M2 14.5l6-5v5l6-5v5l6-5"/><path d="M2 21v-4h5v4h5v-4h5v4h3"/>',
 'xy':'<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><path d="M10.4 8.2L12 6.4l1.6 1.8M10.4 15.8L12 17.6l1.6-1.8M8.2 10.4L6.4 12l1.8 1.6M15.8 10.4l1.8 1.6-1.8 1.6"/>',
 'matrix':'<g stroke-opacity=".5"><path d="M5 5h14M5 12h14M5 19h14M5 5v14M12 5v14M19 5v14"/></g><g fill="currentColor" stroke="none"><circle cx="5" cy="5" r="1.9"/><circle cx="12" cy="5" r="1.9"/><circle cx="19" cy="5" r="1.9"/><circle cx="5" cy="12" r="1.9"/><circle cx="12" cy="12" r="1.9"/><circle cx="19" cy="12" r="1.9"/><circle cx="5" cy="19" r="1.9"/><circle cx="12" cy="19" r="1.9"/><circle cx="19" cy="19" r="1.9"/></g>',
 'fx':'<path d="M10.5 3.5Q11.4 9.6 17.5 10.5Q11.4 11.4 10.5 17.5Q9.6 11.4 3.5 10.5Q9.6 9.6 10.5 3.5Z"/><path d="M19 3v5M16.5 5.5h5"/><circle cx="18.5" cy="18.5" r="1.3" fill="currentColor" stroke="none"/><path d="M13.5 20.5l1.6-1.6" />',
 'seq':'<rect x="2.8" y="7.5" width="3" height="9" rx="1" fill="currentColor"/><rect x="8.3" y="7.5" width="3" height="9" rx="1"/><rect x="13.8" y="7.5" width="3" height="9" rx="1" fill="currentColor"/><rect x="19.3" y="7.5" width="3" height="9" rx="1"/><path d="M4.3 20.5h.01M9.8 20.5h.01M15.3 20.5h.01M20.8 20.5h.01" stroke-width="2"/>',
 'lfo':'<path d="M2 12c3.3-9 6.7-9 10 0s6.7 9 10 0"/><path d="M2 12h20" stroke-opacity=".35"/>',
 'xmod':'<path d="M2 12q1.5-5 3 0t3 0t3 0t3 0t3 0t3 0t3 0" stroke-opacity=".7"/><path d="M2 12c3.3 9 6.7 9 10 0s6.7-9 10 0"/><circle cx="12" cy="12" r="1.7" fill="currentColor" stroke="none"/>',
 'vocoder':'<rect x="3.5" y="2.5" width="6" height="10" rx="3"/><path d="M1.5 11c0 2.76 2.24 5 5 5s5-2.24 5-5M6.5 16v3.5M4 19.5h5"/><path d="M15.5 9v6M18.5 6v12M21.5 8.5v7"/>',
 'scope':'<rect x="2.5" y="4" width="19" height="13.5" rx="2.5"/><path d="M5 11h2l2-4.5 2.5 8 2-6 1.2 2.5H19"/><path d="M8 21h8"/>',
 'mixer':'<path d="M4.5 3v18M9.5 3v18M14.5 3v18M19.5 3v18"/><g fill="currentColor" stroke="none"><rect x="2.3" y="14" width="4.4" height="3.2" rx=".9"/><rect x="7.3" y="7" width="4.4" height="3.2" rx=".9"/><rect x="12.3" y="11" width="4.4" height="3.2" rx=".9"/><rect x="17.3" y="4.5" width="4.4" height="3.2" rx=".9"/></g>',
 'c64':'<rect x="6" y="6" width="12" height="12" rx="1.6"/><rect x="9.2" y="9.2" width="5.6" height="5.6" rx=".6" stroke-opacity=".5"/><path d="M9 3v3M12 3v3M15 3v3M9 18v3M12 18v3M15 18v3M3 9h3M3 12h3M3 15h3M18 9h3M18 12h3M18 15h3"/>',
 'spec':'<g fill="currentColor" fill-opacity=".22"><rect x="3.5" y="11" width="3.2" height="9"/><rect x="8" y="5" width="3.2" height="15"/><rect x="12.5" y="13" width="3.2" height="7"/><rect x="17" y="3.5" width="3.2" height="16.5"/></g><path d="M2.5 20.5h19"/>',
 'presets':'<path d="M9 3.8h11a1.5 1.5 0 0 1 1.5 1.5v10.5"/><path d="M6 6.8h11a1.5 1.5 0 0 1 1.5 1.5v9"/><rect x="2.5" y="9.8" width="13.5" height="11" rx="1.8"/><path d="M5.5 14h7M5.5 17.2h4.5"/>',
 'settings':'<path d="%s"/><circle cx="12" cy="12" r="2.8"/>'%GEAR,
 'sync':'<path d="M2 9l5-5.5v5.5l5-5.5v5.5l5-5.5v5.5"/><path d="M7 11.5v3.6M12 11.5v3.6M17 11.5v3.6M5.8 14l1.2 1.4 1.2-1.4M10.8 14l1.2 1.4 1.2-1.4M15.8 14l1.2 1.4 1.2-1.4" stroke-opacity=".75"/><path d="M2 21.5c1.6-5 3.4-5 5 0M7 21.5c1.6-5 3.4-5 5 0M12 21.5c1.6-5 3.4-5 5 0M17 21.5c1.6-5 3.4-5 5 0"/>',
 'ringmod':'<circle cx="8.6" cy="12" r="5.6"/><circle cx="15.4" cy="12" r="5.6"/><path d="M12 8.2c1.6 1.1 2.4 2.4 2.4 3.8s-.8 2.7-2.4 3.8c-1.6-1.1-2.4-2.4-2.4-3.8s.8-2.7 2.4-3.8z" fill="currentColor" fill-opacity=".35" stroke="none"/><path d="M2 3.2q1.1-2 2.2 0t2.2 0M17.6 3.2q1.1-2 2.2 0t2.2 0" stroke-opacity=".6"/>',
 'automation':'<path d="M3 18C8 18 8 6 12 6s4 12 9 12"/><circle cx="3" cy="18" r="1.4" fill="currentColor" stroke="none"/><circle cx="21" cy="18" r="1.4" fill="currentColor" stroke="none"/><circle cx="12" cy="6" r="1.4" fill="currentColor" stroke="none"/>',
}

ORDER = ['osc','xy','matrix','fx','seq','lfo','xmod','vocoder','scope','mixer','c64','spec','presets','settings','automation','sync','ringmod']
NUM = re.compile(r'-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?')
def path_cmds(d):
    """SVG path data -> absolute ('M',x,y) ('L',x,y) ('Q',cx,cy,x,y) ('C',x1,y1,x2,y2,x,y) ('Z',) list (M L H V C S Q T Z, relative or absolute)."""
    toks = re.findall(r'[MmLlHhVvCcSsQqTtZz]|' + NUM.pattern, d); out = []; i = 0; cx = cy = sx = sy = 0.0; cmd = None; lastc = None; lastq = None
    def num():
        nonlocal i; v = float(toks[i]); i += 1; return v
    while i < len(toks):
        if re.match(r'[A-Za-z]', toks[i]): cmd = toks[i]; i += 1
        rel = cmd.islower(); c = cmd.upper(); ox, oy = (cx, cy) if rel else (0.0, 0.0)
        if c == 'Z': out.append(('Z',)); cx, cy = sx, sy; lastc = lastq = None; continue
        if c == 'M': x = num() + ox; y = num() + oy; out.append(('M', x, y)); cx, cy = sx, sy = x, y; cmd = 'l' if rel else 'L'; lastc = lastq = None
        elif c == 'L': x = num() + ox; y = num() + oy; out.append(('L', x, y)); cx, cy = x, y; lastc = lastq = None
        elif c == 'H': x = num() + ox; out.append(('L', x, cy)); cx = x; lastc = lastq = None
        elif c == 'V': y = num() + oy; out.append(('L', cx, y)); cy = y; lastc = lastq = None
        elif c == 'C': a = [num() + (ox if k % 2 == 0 else oy) for k in range(6)]; out.append(('C',) + tuple(a)); lastc = (a[2], a[3]); cx, cy = a[4], a[5]; lastq = None
        elif c == 'S':
            a = [num() + (ox if k % 2 == 0 else oy) for k in range(4)]; r1 = (2 * cx - lastc[0], 2 * cy - lastc[1]) if lastc else (cx, cy)
            out.append(('C', r1[0], r1[1], a[0], a[1], a[2], a[3])); lastc = (a[0], a[1]); cx, cy = a[2], a[3]; lastq = None
        elif c == 'Q': a = [num() + (ox if k % 2 == 0 else oy) for k in range(4)]; out.append(('Q',) + tuple(a)); lastq = (a[0], a[1]); cx, cy = a[2], a[3]; lastc = None
        elif c == 'T':
            x = num() + ox; y = num() + oy; q = (2 * cx - lastq[0], 2 * cy - lastq[1]) if lastq else (cx, cy)
            out.append(('Q', q[0], q[1], x, y)); lastq = q; cx, cy = x, y; lastc = None
        else: raise ValueError(cmd)
    return out
def elements(inner):
    root = ET.fromstring('<g xmlns:x="x">' + inner + '</g>'); els = []
    def walk(node, inh):
        for ch in node:
            st = dict(inh); st.update({k: v for k, v in ch.attrib.items() if k in ('fill', 'stroke', 'stroke-opacity', 'fill-opacity', 'stroke-width')})
            if ch.tag == 'g': walk(ch, st); continue
            fill = st.get('fill', 'none') == 'currentColor'; fa = float(st.get('fill-opacity', 1)) if fill else 0.0
            stroke = st.get('stroke', 'currentColor') != 'none'; sa = float(st.get('stroke-opacity', 1)); lw = float(st.get('stroke-width', 1.6))
            e = {'fa': fa, 'stroke': stroke, 'sa': sa, 'lw': lw}
            if ch.tag == 'path':
                cm = path_cmds(ch.attrib['d'])
                if len(cm) == 2 and cm[0][0] == 'M' and cm[1][0] == 'L' and abs(cm[1][1] - cm[0][1]) < 0.05 and abs(cm[1][2] - cm[0][2]) < 0.05:   # zero-length round-capped line = a dot
                    e.update(kind='c', v=(cm[0][1], cm[0][2], lw / 2), fa=sa, stroke=False)
                else: e.update(kind='p', v=cm)
            elif ch.tag == 'rect': e.update(kind='r', v=(float(ch.attrib['x']), float(ch.attrib['y']), float(ch.attrib['width']), float(ch.attrib['height']), float(ch.attrib.get('rx', 0))))
            elif ch.tag == 'circle': e.update(kind='c', v=(float(ch.attrib['cx']), float(ch.attrib['cy']), float(ch.attrib['r'])))
            else: raise ValueError(ch.tag)
            els.append(e)
    walk(root, {}); return els
def f(x): return ('%.3f' % x).rstrip('0').rstrip('.') or '0'
def cpp():
    o = ['// GENERATED by tools/export_nav_icons.py. Do not edit.', '// The web app\'s icon-only tab bar art (24x24 grid, 1.6 stroke, round caps) drawn with juce::Graphics in any colour.', '#pragma once', '#include <juce_gui_basics/juce_gui_basics.h>', '', 'namespace vv { namespace navicon {', '',
         'enum Id { Osc, XY, Matrix, FX, Seq, LFO, CrossMod, Vocoder, Scope, Mixer, C64, Spectrogram, Presets, Settings, Automation, Sync, RingMod, Count };', '',
         'inline void drawPath(juce::Graphics& g, const juce::Path& p, const juce::AffineTransform& t, juce::Colour c, bool stroke, float sa, float lw, float fa) {',
         '    if (fa > 0.f) { g.setColour(c.withMultipliedAlpha(fa)); g.fillPath(p, t); }',
         '    if (stroke) { g.setColour(c.withMultipliedAlpha(sa)); g.strokePath(p, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t); }',
         '}', '',
         '// Draws icon `id` centred in `box` (scaled to fit) in colour `c`.', 'inline void draw(juce::Graphics& g, Id id, juce::Rectangle<float> box, juce::Colour c) {',
         '    const float s = juce::jmin(box.getWidth(), box.getHeight()) / 24.f; const auto t = juce::AffineTransform::scale(s).translated(box.getCentreX() - 12.f * s, box.getCentreY() - 12.f * s);',
         '    juce::Path p;', '    switch (id) {']
    for idx, k in enumerate(ORDER):
        o.append('    case %s: {' % ['Osc','XY','Matrix','FX','Seq','LFO','CrossMod','Vocoder','Scope','Mixer','C64','Spectrogram','Presets','Settings','Automation','Sync','RingMod'][idx])
        for e in elements(ICONS[k]):
            o.append('        p.clear();')
            if e['kind'] == 'p':
                for c in e['v']:
                    if c[0] == 'M': o.append('        p.startNewSubPath(%sf, %sf);' % (f(c[1]), f(c[2])))
                    elif c[0] == 'L': o.append('        p.lineTo(%sf, %sf);' % (f(c[1]), f(c[2])))
                    elif c[0] == 'Q': o.append('        p.quadraticTo(%sf, %sf, %sf, %sf);' % tuple(f(x) for x in c[1:]))
                    elif c[0] == 'C': o.append('        p.cubicTo(%sf, %sf, %sf, %sf, %sf, %sf);' % tuple(f(x) for x in c[1:]))
                    else: o.append('        p.closeSubPath();')
            elif e['kind'] == 'r':
                x, y, w, h, r = e['v']; o.append('        p.addRoundedRectangle(%sf, %sf, %sf, %sf, %sf);' % (f(x), f(y), f(w), f(h), f(r)))
            else:
                cx, cy, r = e['v']; o.append('        p.addEllipse(%sf, %sf, %sf, %sf);' % (f(cx - r), f(cy - r), f(2 * r), f(2 * r)))
            o.append('        drawPath(g, p, t, c, %s, %sf, %sf, %sf);' % ('true' if e['stroke'] else 'false', f(e['sa']), f(e['lw']), f(e['fa'])))
        o.append('        break; }')
    o += ['    default: break;', '    }', '}', '', '} } // namespace vv::navicon', '']
    return re.sub(r'(?<![\w.])(-?\d+)f\b', r'\1.f', '\n'.join(o))
def swift():
    names = ['osc','xy','matrix','fx','seq','lfo','crossMod','vocoder','scope','mixer','c64','spectrogram','presets','settings','automation','sync','ringmod']
    o = ['// GENERATED by tools/export_nav_icons.py. Do not edit.', '// The web app\'s icon-only tab bar art (24x24 grid, 1.6 stroke, round caps) as SwiftUI paths, plus a template-image renderer for TabView items.',
         'import SwiftUI', '#if canImport(UIKit)', 'import UIKit', '#elseif canImport(AppKit)', 'import AppKit', '#endif', '',
         'enum NavIcon: CaseIterable {', '    case ' + ', '.join(names), '',
         '    /// One drawable element: a path with its stroke / fill style (alphas are relative to the colour the icon is drawn in).',
         '    struct Element { let path: Path; let stroke: Bool; let strokeAlpha: Double; let lineWidth: CGFloat; let fillAlpha: Double }', '',
         '    var elements: [Element] {', '        switch self {']
    for n, k in zip(names, ORDER):
        o.append('        case .%s:' % n); o.append('            var e: [Element] = []')
        for e in elements(ICONS[k]):
            o.append('            do { var p = Path()')
            if e['kind'] == 'p':
                for c in e['v']:
                    if c[0] == 'M': o.append('                p.move(to: CGPoint(x: %s, y: %s))' % (f(c[1]), f(c[2])))
                    elif c[0] == 'L': o.append('                p.addLine(to: CGPoint(x: %s, y: %s))' % (f(c[1]), f(c[2])))
                    elif c[0] == 'Q': o.append('                p.addQuadCurve(to: CGPoint(x: %s, y: %s), control: CGPoint(x: %s, y: %s))' % (f(c[3]), f(c[4]), f(c[1]), f(c[2])))
                    elif c[0] == 'C': o.append('                p.addCurve(to: CGPoint(x: %s, y: %s), control1: CGPoint(x: %s, y: %s), control2: CGPoint(x: %s, y: %s))' % (f(c[5]), f(c[6]), f(c[1]), f(c[2]), f(c[3]), f(c[4])))
                    else: o.append('                p.closeSubpath()')
            elif e['kind'] == 'r':
                x, y, w, h, r = e['v']; o.append('                p.addRoundedRect(in: CGRect(x: %s, y: %s, width: %s, height: %s), cornerSize: CGSize(width: %s, height: %s))' % (f(x), f(y), f(w), f(h), f(r), f(r)))
            else:
                cx, cy, r = e['v']; o.append('                p.addEllipse(in: CGRect(x: %s, y: %s, width: %s, height: %s))' % (f(cx - r), f(cy - r), f(2 * r), f(2 * r)))
            o.append('                e.append(Element(path: p, stroke: %s, strokeAlpha: %s, lineWidth: %s, fillAlpha: %s)) }' % ('true' if e['stroke'] else 'false', f(e['sa']), f(e['lw']), f(e['fa'])))
        o.append('            return e')
    o += ['        }', '    }', '}', '',
          '/// The icon as a SwiftUI view in the current foreground colour (scales to its frame).',
          'struct NavIconView: View {', '    let icon: NavIcon', '    var body: some View {', '        Canvas { ctx, size in',
          '            let s = min(size.width, size.height) / 24; let t = CGAffineTransform(scaleX: s, y: s)',
          '            let ox = (size.width - 24 * s) / 2, oy = (size.height - 24 * s) / 2', '            for e in icon.elements {',
          '                let p = e.path.applying(t).offsetBy(dx: ox, dy: oy)',
          '                if e.fillAlpha > 0 { var c = ctx; c.opacity = e.fillAlpha; c.fill(p, with: .foreground) }',
          '                if e.stroke { var c = ctx; c.opacity = e.strokeAlpha; c.stroke(p, with: .foreground, style: StrokeStyle(lineWidth: e.lineWidth * s, lineCap: .round, lineJoin: .round)) }',
          '            }', '        }', '        .aspectRatio(1, contentMode: .fit)', '    }', '}', '',
          '/// Template images for TabView items (the system tab bar only draws Image and Text, not custom views).',
          'extension NavIcon {',
          '    #if canImport(UIKit)',
          '    func image(points: CGFloat = 25) -> Image {',
          '        let r = UIGraphicsImageRenderer(size: CGSize(width: points, height: points))',
          '        let img = r.image { rc in',
          '            let g = rc.cgContext; let s = points / 24; g.setLineCap(.round); g.setLineJoin(.round)',
          '            for e in elements {',
          '                let p = e.path.applying(CGAffineTransform(scaleX: s, y: s)).cgPath',
          '                if e.fillAlpha > 0 { g.setFillColor(UIColor.black.withAlphaComponent(e.fillAlpha).cgColor); g.addPath(p); g.fillPath() }',
          '                if e.stroke { g.setStrokeColor(UIColor.black.withAlphaComponent(e.strokeAlpha).cgColor); g.setLineWidth(e.lineWidth * s); g.addPath(p); g.strokePath() }',
          '            }', '        }',
          '        return Image(uiImage: img.withRenderingMode(.alwaysTemplate))', '    }',
          '    #elseif canImport(AppKit)',
          '    func image(points: CGFloat = 25) -> Image {',
          '        let img = NSImage(size: NSSize(width: points, height: points), flipped: true) { _ in',
          '            guard let g = NSGraphicsContext.current?.cgContext else { return false }',
          '            let s = points / 24; g.setLineCap(.round); g.setLineJoin(.round)',
          '            for e in elements {',
          '                let p = e.path.applying(CGAffineTransform(scaleX: s, y: s)).cgPath',
          '                if e.fillAlpha > 0 { g.setFillColor(NSColor.black.withAlphaComponent(e.fillAlpha).cgColor); g.addPath(p); g.fillPath() }',
          '                if e.stroke { g.setStrokeColor(NSColor.black.withAlphaComponent(e.strokeAlpha).cgColor); g.setLineWidth(e.lineWidth * s); g.addPath(p); g.strokePath() }',
          '            }', '            return true', '        }', '        img.isTemplate = true', '        return Image(nsImage: img)', '    }',
          '    #endif', '}', '']
    return '\n'.join(o)
if __name__ == '__main__':
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    open(os.path.join(root, 'Juce/Source/NavIcons.h'), 'w').write(cpp()); open(os.path.join(root, 'Apple/Shared/NavIcons.swift'), 'w').write(swift())
    print('wrote NavIcons.h and NavIcons.swift for', len(ORDER), 'icons')
