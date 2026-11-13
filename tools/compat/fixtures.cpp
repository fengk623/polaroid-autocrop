// FK-014 (U028) compatibility fixtures for tests/core/test_compat.cpp (tests/data/compat/README.md). Not part
// of the build: tools/compat/make-fixtures.sh compiles it against the core of each release.
//   gen write DIR            writes project.fkproj, export_01.fk.json and jig.fktemplate with that release's
//   code gen roundtrip IN OUT     opens IN with that release's loader and saves it to OUT (what a rollback
//   does)
// Built with -DFK_FIXTURE_CURRENT against the current core, "write" also fills the fields added after
// 0.6.0-alpha.1 (automatic count, scan rotation, per-print mask, relink transform, output settings, export
// records, ICC profiles), so that a rollback round trip shows what an earlier release keeps.
// Content is synthetic: invented paths and hashes, no image data.
#include "fk/project.hpp"
#ifdef FK_FIXTURE_CURRENT
#include "fk/color.hpp"
#endif

#include <cstdio>
#include <cstring>
#include <string>

using namespace fk;

static Quad rect(double x0, double y0, double x1, double y1) {
    Quad q;
    q.p = {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}};
    return q;
}

static int fail(const char* what, const Error& e) {
    std::fprintf(stderr, "%s: %s\n", what, e.message.c_str());
    return 1;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "write") == 0) {
        const std::filesystem::path dir = argv[2];
        std::filesystem::create_directories(dir);
        Project p = newProject("Compatibility fixture");
        p.projectId = "8b0c7c36-5d2e-4f51-9a3e-2f7a1d0c4e11";
        p.createdAt = "2026-10-01T10:00:00Z";
        p.modifiedAt = "2026-10-01T11:00:00Z";
        p.settings.expectedCount = 2;
        p.settings.safetyMarginPx = 1.5;
        p.settings.exportFolder = "/Users/example/Exports";
        p.settings.exportFormat = "tif";
        p.settings.exportBits = 16;
        p.settings.exportCompression = "zip";
        p.settings.colorPolicy = "keep";
        p.settings.privacy = "keep_all_safe";

        ScanRecord s;
        s.scanId = "scan-0001";
        s.path = "/Users/example/Scans/roll-01.tif";
        s.relPath = "Scans/roll-01.tif";
        s.fingerprint.size = 123456789;
        s.fingerprint.mtime = "2026-09-30T08:00:00Z";
        s.fingerprint.quickSha256 = std::string(64, 'a');
        s.info.width = 6000;
        s.info.height = 4000;
        s.info.channels = 3;
        s.info.bitsPerSample = 16;
        s.info.dpiX = s.info.dpiY = 600;
        s.info.iccDescription = "Adobe RGB (1998)";
        s.iccSha256 = std::string(64, 'b');
        s.detectedAt = "2026-10-01T10:05:00Z";
        s.expectedCount = 2;
        s.dpiUsed = 600;
        s.proxyScale = 4;
        s.warnings = {"count_mismatch"};

        ItemRecord a;
        a.itemId = "item-0001";
        a.index = 1;
        a.label = "Beach";
        a.filmType = "instax_mini";
        a.outer = Shape::fromQuad(rect(100, 120, 1300, 2020));
        a.cornerRadiusPx = 12;
        a.outerSource = BoundarySource::manual;
        a.inner = Shape::fromQuad(rect(180, 260, 1220, 1660));
        a.innerSource = BoundarySource::automatic;
        a.upright = 1;
        a.confidence = 0.97;
        a.outerConfidence = 0.98;
        a.innerConfidence = 0.95;
        a.reasons = {"ok"};
        a.reviewed = true;
        EditVersion ev;
        ev.editVersionId = "ev-0001";
        ev.version = 1;
        ev.path = "/Users/example/Exports/roll-01_01_v1.tif";
        ev.sidecar = "/Users/example/Exports/roll-01_01_v1.fk.json";
        ev.exportedAt = "2026-10-01T10:30:00Z";
        ev.fileSha256 = std::string(64, 'c');
        ev.format = "tif";
        ev.relinkedPath = "/Users/example/Graded/roll-01_01_v1.tif";
        ev.relinkedSha256 = std::string(64, 'd');
        ev.matchedBy = "xmp";
        ev.relinkedAt = "2026-10-01T10:50:00Z";
        a.edits.push_back(ev);
#ifndef FK_FIXTURE_CURRENT // per-print placeholders of releases before 0.7.0-alpha.5 (retired)
        a.composite.maskMode = MaskMode::feather;
        a.composite.featherPx = 3;
        a.composite.preserveEdgePx = 0;
#endif
        MaskStroke st;
        st.points = {Vec2{300, 400}, Vec2{320, 420}};
        st.radiusPx = 15;
        st.add = false;
        a.composite.strokes.push_back(st);
        a.composite.lastOutput = "/Users/example/Exports/roll-01_01_final.tif";
        a.composite.lastOutputAt = "2026-10-01T10:55:00Z";
        a.finalExport = "/Users/example/Exports/roll-01_01_final.tif";

        ItemRecord b;
        b.itemId = "item-0002";
        b.index = 2;
        b.label = "Round";
        b.filmType = "instax_square";
        b.outer = Shape::fromQuad(rect(1500, 120, 3100, 2020));
        b.inner = Shape::fromEllipse(Ellipse{Vec2{2300, 1000}, 600, 590, 0.0});
        b.excluded = false;
        b.confidence = 0.9;
#ifdef FK_FIXTURE_CURRENT
        p.settings.autoCount = false;
        p.settings.outputJson = R"({"geometry":"straightened","final":{"format":"tif","bits":16}})";
        s.autoCount = true;
        s.rotation = 3;
        a.composite.mask = MaskChoice{MaskMode::feather, 0.05, true};
        a.composite.maskSet = true;
        RelinkTransform& rt = a.edits[0].relinkTransform;
        rt.set = true;
        rt.rotate = 1;
        rt.rotateSource = "content";
        rt.scale = true;
        rt.contentScore = 0.93;
        rt.gradedSize = SizeI{1400, 1040};
        rt.iccSource = "file";
        const std::vector<uint8_t> icc(64, 7); // invented bytes standing for a chosen profile
        rt.iccSha256 = sha256Hex(icc.data(), icc.size());
        p.iccProfiles[rt.iccSha256] = icc;
#endif
        s.items = {a, b};
        p.scans.push_back(s);
        if (Status st2 = saveProject(p, dir / "project.fkproj"); !st2.ok())
            return fail("saveProject", st2.error());

        Sidecar sc;
        sc.projectId = p.projectId;
        sc.scanId = s.scanId;
        sc.itemId = a.itemId;
        sc.editVersionId = ev.editVersionId;
        sc.label = a.label;
        sc.version = 1;
        sc.scanFile = "roll-01.tif";
        sc.scanFingerprint = s.fingerprint;
        sc.inner = a.inner;
        sc.outer = a.outer;
        sc.geometry.outSize = SizeI{1040, 1400};
        sc.geometry.outToScan = Mat3::translate(180, 260);
        sc.geometry.scanToOut = Mat3::translate(-180, -260);
        sc.geometry.sourceRect = RectI{180, 260, 1040, 1400};
        sc.sourceSize = SizeI{6000, 4000};
        sc.exportSize = SizeI{1040, 1400};
        sc.bits = 16;
        sc.channels = 3;
        sc.iccDescription = "Adobe RGB (1998)";
        sc.iccSha256 = s.iccSha256;
        sc.dpiX = sc.dpiY = 600;
        sc.fileName = "roll-01_01_v1.tif";
        sc.fileSha256 = ev.fileSha256;
        sc.signature.valid = true;
        for (int i = 0; i < Signature::kSide * Signature::kSide; ++i)
            sc.signature.v[static_cast<size_t>(i)] = static_cast<float>((i * 37) % 101) / 100.0f;
        sc.software = "FrameKeeper";
        sc.exportedAt = ev.exportedAt;
        if (Status st2 = saveSidecar(sc, dir / "export_01.fk.json"); !st2.ok())
            return fail("saveSidecar", st2.error());
#ifdef FK_FIXTURE_CURRENT
        p.exportRecords.push_back(sc); // UX-07: the record is kept in the project too
        if (Status st2 = saveProject(p, dir / "project.fkproj"); !st2.ok())
            return fail("saveProject", st2.error());
        std::filesystem::remove(dir / "project.fkproj.bak");
#endif

        Template t;
        t.templateId = "tmpl-0001";
        t.name = "Two-up jig";
        t.createdDpi = 600;
        t.scanSize = SizeI{6000, 4000};
        TemplateSlot s1;
        s1.index = 1;
        s1.filmType = "instax_mini";
        s1.outer = {Vec2{0.02, 0.03}, Vec2{0.22, 0.03}, Vec2{0.22, 0.5}, Vec2{0.02, 0.5}};
        s1.inner = Shape::fromQuad(rect(0.05, 0.06, 0.2, 0.4));
        TemplateSlot s2;
        s2.index = 2;
        s2.filmType = "instax_square";
        s2.outer = {Vec2{0.25, 0.03}, Vec2{0.52, 0.03}, Vec2{0.52, 0.5}, Vec2{0.25, 0.5}};
        s2.inner = Shape::fromEllipse(Ellipse{Vec2{0.38, 0.25}, 0.1, 0.1, 0.0});
        t.slots = {s1, s2};
        if (Status st2 = saveTemplate(t, dir / "jig.fktemplate"); !st2.ok())
            return fail("saveTemplate", st2.error());
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "roundtrip") == 0) {
        const std::filesystem::path in = argv[2], out = argv[3];
        const std::string name = in.filename().string();
        if (name.ends_with(".fkproj")) {
            OpenMode mode = OpenMode::read_write;
            auto p = loadProject(in, &mode);
            if (!p) return fail("loadProject", p.error());
            if (mode != OpenMode::read_write) std::fprintf(stderr, "opened read-only\n");
            if (Status st = saveProject(*p, out); !st.ok()) return fail("saveProject", st.error());
        } else if (name.ends_with(".fk.json")) {
            auto s = loadSidecar(in);
            if (!s) return fail("loadSidecar", s.error());
            if (Status st = saveSidecar(*s, out); !st.ok()) return fail("saveSidecar", st.error());
        } else {
            OpenMode mode = OpenMode::read_write;
            auto t = loadTemplate(in, &mode);
            if (!t) return fail("loadTemplate", t.error());
            if (Status st = saveTemplate(*t, out); !st.ok()) return fail("saveTemplate", st.error());
        }
        return 0;
    }
    std::fprintf(stderr, "usage: gen write DIR | gen roundtrip IN OUT\n");
    return 2;
}
