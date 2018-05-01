// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "G3d.h"
#include "BufferedA3dStream.h"
#include "Buffer.h"
#include "FrameIO.h"
#include "Polygon.h"
#include "GMesh.h"
#include "FileIO.h"
#include "MeshOp.h"
#include "StringOp.h"
#include "Args.h"
using namespace hh;

namespace g3d {

namespace {

int robn;                       // current a3d object to read
string filename = "-";
float wait_command;

void add_extra(int n) {
    if (!n) return;
    int oobn = robn-1;
    assertx(oobn>=g_obs.first);
    if (!terse) showf(" (+%d)\n", n);
    for_int(i, n) {
        assertx(g_obs.legal(robn));
        g_obs.copy(oobn, robn);
        g_obs.last = max(g_obs.last, robn);
        HB::make_segment_link(oobn, robn);
        robn++;
    }
}

bool opened = false;
int total_gons = 0, total_lines = 0, total_points = 0;
int total_vertices = 0, total_faces = 0;

void open_if_closed() {
    if (opened) return;
    assertx(g_obs.legal(robn));
    opened = true;
    total_gons = total_lines = total_points = 0;
    total_vertices = total_faces = 0;
    g_obs.last = max(g_obs.last, robn);
    g_obs[robn].update();
    HB::open_segment(robn);
}

int decode_obn(const A3dElem& el) {
    int obn = robn;
    switch (int(el.f()[0])) {
     bcase 0:
        obn = int(el.f()[1]+.5f);
     bcase 1:
        obn += int(el.f()[1]+.5f);
     bcase 2:
        obn = int(g_obs.last-el.f()[1]+.5f);
     bdefault:
        obn = 1;
        if (Warning("bad object number code")) SHOW(int(el.f()[0]));
    }
    assertx(g_obs.legal(obn));
    return obn;
}

A3dElem::EType read_a3delem(RA3dStream& ia3d) {
    A3dElem el; ia3d.read(el);
    A3dElem::EType elt = el.type();
    if (elt==A3dElem::EType::polygon || elt==A3dElem::EType::polyline || elt==A3dElem::EType::point) {
        open_if_closed();
        switch (elt) {
         bcase A3dElem::EType::polygon: total_gons++;
         bcase A3dElem::EType::polyline: total_lines++;
         bcase A3dElem::EType::point: total_points++;
         bdefault: assertnever(string() + "unknown type '" + narrow_cast<char>(elt) + "'");
        }
        for_int(i, el.num()) {
            g_obs[robn].enter_point(el[i].p);
        }
        HB::segment_add_object(el);
        return elt;
    }
    if (elt!=A3dElem::EType::endframe &&
        elt!=A3dElem::EType::endobject &&
        elt!=A3dElem::EType::editobject &&
        elt!=A3dElem::EType::endfile) return elt;
    CloseIfOpen();
    if (elt==A3dElem::EType::editobject) {
        assertx(!el.f()[2]);
        int obn = decode_obn(el);
        g_obs[obn].clear();
        HB::clear_segment(obn);
    } else if (elt==A3dElem::EType::endobject) {
        robn = decode_obn(el);
    }
    return elt;
}

void read_mesh_line(char* s) {
    int fi = 0, vi = 0, vi1 = 0, vi2 = 0, vspl1 = 0, vspl2 = 0, vspl3 = 0, vspl4 = 0;
    open_if_closed();
    GMesh& mesh = *g_obs[robn].get_mesh();
    if (sscanf(s, "Vertex %d", &vi)!=1 && sscanf(s, "MVertex %d", &vi)!=1) vi = 0;
    if (sscanf(s, "Face %d", &fi)!=1) fi = 0;
    if (sscanf(s, "Ecol %d %d", &vi1, &vi2)!=2) vi1 = vi2 = 0;
    if (sscanf(s, "Vspl %d %d %d %d", &vspl1, &vspl2, &vspl3, &vspl4)!=4)
        vspl1 = vspl2 = vspl3 = vspl4 = 0;
    mesh.read_line(s);
    // (Bug: Not all mesh transformations clear vflag_ok, fflag_ok flags)
    if (vi1) {
        mesh.flags(mesh.id_vertex(vi1)).flag(vflag_ok) = false;
        for (Face f : mesh.faces(mesh.id_vertex(vi1))) {
            mesh.flags(f).flag(fflag_ok) = false;
        }
    }
    if (vi) {
        total_vertices++;
        Vertex v = mesh.id_vertex(vi);
        g_obs[robn].enter_point(mesh.point(v));
        mesh.flags(v).flag(vflag_ok) = false;
        if (GMesh::string_has_key(mesh.get_string(v), "Opos")) {
            if (!lod_mode) Warning("Entering lod_mode");
            lod_mode = true;
            Point po;
            assertx(parse_key_vec(mesh.get_string(v), "Opos", po));
            g_obs[robn].enter_point(po);
        }
    }
    if (fi) {
        total_faces++;
        Face f = mesh.id_retrieve_face(fi); // it may not have been created if !legal_create_face()
        if (f) mesh.flags(f).flag(fflag_ok) = false;
    }
    if (vspl1) {
        mesh.flags(mesh.id_vertex(vspl1)).flag(vflag_ok) = false;
    }
    mesh.gflags().flag(mflag_ok) = false;
}

bool try_g3d_command(const string& pstr) {
    string str = pstr;
    if (0) {
    } else if (remove_at_beginning(str, "keys ")) {
        // assertx(str.size()==1); // new 20121213
        // KeyPressed(str);
        for (char ch : str) { KeyPressed(string(1, ch)); }
        return true;
    } else if (remove_at_beginning(str, "wait ")) {
        wait_command = Args::parse_float(str);
        return true;
    } else if (remove_at_beginning(str, "lod ")) {
        float f = Args::parse_float(str);
        HB::escape(reinterpret_cast<void*>(1), &f);
        return true;
    } else if (remove_at_beginning(str, "screen_thresh ")) {
        float f = Args::parse_float(str);
        HB::escape(reinterpret_cast<void*>(2), &f);
        return true;
    }
    return false;
}

enum class ETryInput { nothing, success, success_frame, eof };

ETryInput try_input(RBuffer& buf, RBufferedA3dStream& ra3d, string& str) {
    if (!buf.num()) return ETryInput::nothing;
    if (buf[0]=='\x0d') {       // DOS eol
        if (buf.num()>=2 && buf[1]=='\x0a') { buf.extract(2); return ETryInput::success; }
        return ETryInput::nothing;
    }
    if (buf[0]=='\n') { buf.extract(1); return ETryInput::success; }
    int i;
    // try A3d
    switch (ra3d.recognize()) {
     bcase RBufferedA3dStream::ERecognize::parse_error:
        assertnever("");
     bcase RBufferedA3dStream::ERecognize::partial:
        return ETryInput::nothing; // partial a3d
     bcase RBufferedA3dStream::ERecognize::yes: {
         A3dElem::EType rr = read_a3delem(ra3d);
         if (rr==A3dElem::EType::endfile) return ETryInput::eof;
         if (rr==A3dElem::EType::endframe) return ETryInput::success_frame;
         return ETryInput::success;
     }
     bcase RBufferedA3dStream::ERecognize::no:
        ;                       // fall-out
     bdefault: assertnever("");
    }
    // try Frame
    switch (FrameIO::recognize(buf)) {
     bcase FrameIO::ERecognize::parse_error:
        assertnever("");
     bcase FrameIO::ERecognize::partial:
        return ETryInput::nothing; // partial frame
     bcase FrameIO::ERecognize::yes: {
         Frame f; int obn; float z; bool bin;
         i = FrameIO::read(buf, f, obn, z, bin);
         if (!assertw(i)) return ETryInput::success;
         UpdateFrame(obn, f, z);
         num_input_frames++;
         return ETryInput::success;
     }
     bcase FrameIO::ERecognize::no:
        ;                       // fall-out
     bdefault: assertnever("");
    }
    if (!buf.extract_line(str)) return ETryInput::nothing; // partial something
    // 20121211: now trailing '\n' has been removed; all still OK?
    {                           // try Mesh
        char* s = const_cast<char*>(str.c_str());
        if (GMesh::recognize_line(s)) {
            read_mesh_line(s);
            return ETryInput::success;
        }
    }
    if (try_g3d_command(str)) return ETryInput::success;
    if (Warning("Parse error")) showf("G3d: cannot parse '%s'\n", str.c_str());
    return ETryInput::success;
}

// ret: eof
bool read_buffer(RBuffer& buf, RBufferedA3dStream& ra3d, bool during_init) {
    string str;
    for (;;) {
        if (buf.eof()) return true;
        if (buf.err()) { perror("RBuffer error:"); HB::quit(); }
        {
            auto ret = try_input(buf, ra3d, str);
            if (ret!=ETryInput::nothing && asynchronousinput) { HB::redraw_later(); cur_needs_redraw = true; }
            if (ret==ETryInput::eof) {
                return true;
            }
            if (ret==ETryInput::success_frame || wait_command) { // end of frame
                HB::redraw_later(); cur_needs_redraw = true;
                break;
            }
            if (ret==ETryInput::success) continue;     // read something
        }
        // nothing recognizable in buffer, so try filling it
        {
            auto ret = buf.refill();
            if (ret==RBuffer::ERefill::no) { // no more data to fill
                if (!asynchronousinput && during_init) {
                    buf.wait_for_input();
                    continue;
                }
                break;
            }
        }
        // got data, try again for more.
    }
    return false;
}

void read_file(int fd, bool during_init) {
    g_obs[robn].clear();
    HB::clear_segment(robn);
    RBuffer fbuf(fd);
    RBufferedA3dStream fa3d(fbuf);
    assertw(read_buffer(fbuf, fa3d, during_init)); // should get eof
    CloseIfOpen();
}

} // namespace

void CloseIfOpen() {
    if (!opened) return;
    opened = false;
    HB::close_segment();
    g_obs[robn].update_stats();
    if (!terse) {
        string s = sform("G3d: (%d) File:%s", robn, filename.c_str());
        if (total_gons+total_lines+total_points) {
            showf("%s %dgons %dlines %dpts\n", s.c_str(), total_gons, total_lines, total_points);
        }
        if (total_vertices+total_faces) {
            const GMesh& mesh = *g_obs[robn].get_mesh();
            if (total_vertices==mesh.num_vertices()) {
                showf("%s\n", s.c_str());
                showf(" %s\n", mesh_genus_string(mesh).c_str());
            } else {
                showf("%s %dv %df\n", s.c_str(), total_vertices, total_faces);
            }
        }
    }
}

void ReadFiles(bool during_init) {
    robn = g_obs.first;
    if (ob1_updated && robn==1) robn++;
    for_int(i, g_aargs1.num()) {
        filename = g_aargs1[i];
        if (filename[0]=='+') {
            add_extra(to_int(filename.substr(1).c_str()));
            continue;
        }
        RFile is(filename);
        read_file(HH_POSIX(fileno)(is.cfile()), during_init);
        if (anglethresh>=0) RecomputeSharpEdges(*g_obs[robn].get_mesh());
        robn++;
    }
    filename = "-";
}

void ReadInput(bool during_init) {
    static RBuffer buf(0);
    static RBufferedA3dStream ra3d(buf);
    assertx(input);
    tried_input = true;
    if (wait_command>0.f) {
        wait_command = max(0.f, wait_command-fchange);
        HB::redraw_later(); cur_needs_redraw = true;
        return;
    }
    bool eof = read_buffer(buf, ra3d, during_init);
    if (eof) {
        HB::redraw_later(); cur_needs_redraw = true;
        CloseIfOpen();
        input = false; HB::watch_fd0(nullptr);
        want_jump = 0;
        if (0 && !terse) SHOW("G3d: eof");
        if (killeof) HB::quit();
    }
}

void WriteOutput() {
    float z = 0.f; Frame ft; const Frame* t;
    int obn = eye_move ? obview : cob;
    if (g_obs[obn].visible() || obn==obview) {
        if (!obn) z = zoom;
        t = &g_obs[obn].t();
    } else {
        FrameIO::create_not_a_frame(ft);
        t = &ft;
    }
    if (!FrameIO::write(std::cout, *t, obn, z, obinary)) {
#if 1 || defined(_WIN32)
        // No SIGPIPE to terminate process in Win32.
        // Unlike exit(), _exit() does not call destructors.
        _exit(0);
#else
        Warning("FrameIO::write failed");
#endif
    }
    std::cout.flush();
}

} // namespace g3d
