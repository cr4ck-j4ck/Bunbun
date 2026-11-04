// Bunbun — the File shelf and the camera Mirror on Linux (see shelf_linux.h).
#include "shelf_linux.h"

#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <thread>

#include "platform.h"
#include "settings.h"

namespace bb::lshelf {
namespace {

bool g_shelfOn = false, g_mirrorOn = false, g_persist = true;
int g_max = 10;
std::vector<Item> g_items;
bool g_loaded = false;

std::string ShelfPath() { return bb::plat::StateDir() + "/shelf.txt"; }

Item ItemFor(const std::string& uri) {
    Item it;
    it.uri = uri;
    GFile* f = g_file_new_for_uri(uri.c_str());
    if (char* base = g_file_get_basename(f)) {
        it.name = base;
        g_free(base);
    }
    it.folder = g_file_query_file_type(f, G_FILE_QUERY_INFO_NONE, nullptr) == G_FILE_TYPE_DIRECTORY;
    g_object_unref(f);
    if (it.name.empty()) it.name = uri;
    return it;
}

void Save() {
    if (!g_persist) return;
    std::string text;
    for (const Item& it : g_items) text += it.uri + "\n";  // only the paths, never the files
    bb::plat::MakeDirs(bb::plat::StateDir());
    bb::plat::WriteFileAtomic(ShelfPath(), text);
}

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string text;
    if (!g_persist || !bb::plat::ReadFile(ShelfPath(), &text)) return;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        const std::string uri = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() : nl + 1;
        if (uri.empty()) continue;
        GFile* f = g_file_new_for_uri(uri.c_str());
        const bool exists = g_file_query_exists(f, nullptr);  // files moved or deleted since drop off
        g_object_unref(f);
        if (exists) g_items.push_back(ItemFor(uri));
    }
}

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    g_shelfOn = s::GetBool(L"Modules.FileTrayModule");
    g_max = std::clamp(s::GetInt(L"Modules.FileTrayMaxItems"), 1, 30);
    g_persist = s::GetBool(L"Modules.FileTrayPersist");
    g_mirrorOn = s::GetBool(L"Bunbun.MirrorPage");
    if (!g_mirrorOn) CameraStop();
    Load();
    if (static_cast<int>(g_items.size()) > g_max) g_items.resize(static_cast<size_t>(g_max));
}

bool ShelfOn() { return g_shelfOn; }
bool MirrorOn() { return g_mirrorOn; }

const std::vector<Item>& Items() { return g_items; }

void Add(const std::vector<std::string>& uris) {
    for (const std::string& uri : uris) {
        if (uri.empty()) continue;
        g_items.erase(std::remove_if(g_items.begin(), g_items.end(), [&](const Item& it) { return it.uri == uri; }), g_items.end());
        g_items.insert(g_items.begin(), ItemFor(uri));
    }
    if (static_cast<int>(g_items.size()) > g_max) g_items.resize(static_cast<size_t>(g_max));
    Save();
}

void Remove(int index) {
    if (index < 0 || index >= static_cast<int>(g_items.size())) return;
    g_items.erase(g_items.begin() + index);
    Save();
}

void Clear() {
    g_items.clear();
    Save();
}

void Open(int index) {
    if (index < 0 || index >= static_cast<int>(g_items.size())) return;
    g_app_info_launch_default_for_uri(g_items[static_cast<size_t>(index)].uri.c_str(), nullptr, nullptr);
}

// ── The camera (V4L2) ───────────────────────────────────────────────────────
// The first capture device, at 640 x 480 in YUYV (or MJPEG when that's all it
// gives), four memory-mapped buffers, read on a thread. Each frame is turned
// into a 320 x 240 RGB picture for the page.

namespace {
std::mutex g_camMutex;
std::atomic<bool> g_camRun{false};
std::thread g_camThread;
std::string g_camError;
std::vector<uint32_t> g_rgb;  // 0xAARRGGBB, guarded by g_camMutex
int g_rgbW = 0, g_rgbH = 0;
bool g_rgbNew = false;
cairo_surface_t* g_surface = nullptr;
std::function<void()> g_onFrame;

int Xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

uint8_t Clamp8(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

void StoreFrame(std::vector<uint32_t>&& rgb, int w, int h) {
    {
        std::lock_guard lock(g_camMutex);
        g_rgb = std::move(rgb);
        g_rgbW = w;
        g_rgbH = h;
        g_rgbNew = true;
    }
    g_idle_add([](gpointer) -> gboolean {
        if (g_onFrame) g_onFrame();
        return G_SOURCE_REMOVE;
    }, nullptr);
}

void FromYuyv(const uint8_t* p, int w, int h, int stride) {
    // Half size: one pixel per 2 x 2 block, from the top-left pair's luma and chroma.
    const int ow = w / 2, oh = h / 2;
    std::vector<uint32_t> rgb(static_cast<size_t>(ow * oh));
    for (int y = 0; y < oh; ++y) {
        const uint8_t* row = p + static_cast<size_t>(y * 2) * stride;
        for (int x = 0; x < ow; ++x) {
            const uint8_t* q = row + x * 4;  // Y0 U Y1 V
            const int c = q[0] - 16, d = q[1] - 128, e = q[3] - 128;
            const uint8_t r = Clamp8((298 * c + 409 * e + 128) >> 8), g = Clamp8((298 * c - 100 * d - 208 * e + 128) >> 8),
                          b = Clamp8((298 * c + 516 * d + 128) >> 8);
            rgb[static_cast<size_t>(y * ow + x)] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
    StoreFrame(std::move(rgb), ow, oh);
}

void FromJpeg(const uint8_t* p, size_t len) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("jpeg", nullptr);
    if (!loader) return;
    gdk_pixbuf_loader_set_size(loader, 320, 240);
    const bool ok = gdk_pixbuf_loader_write(loader, p, len, nullptr) && gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf* pb = ok ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    if (pb) {
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), n = gdk_pixbuf_get_n_channels(pb),
                  stride = gdk_pixbuf_get_rowstride(pb);
        const guchar* px = gdk_pixbuf_read_pixels(pb);
        std::vector<uint32_t> rgb(static_cast<size_t>(w * h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const guchar* q = px + y * stride + x * n;
                rgb[static_cast<size_t>(y * w + x)] = 0xff000000u | (q[0] << 16) | (q[1] << 8) | q[2];
            }
        StoreFrame(std::move(rgb), w, h);
    }
    g_object_unref(loader);
}

void CameraLoop() {
    int fd = -1;
    for (int i = 0; i < 8 && fd < 0; ++i) {
        const std::string dev = "/dev/video" + std::to_string(i);
        const int f = open(dev.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (f < 0) continue;
        v4l2_capability cap{};
        if (Xioctl(f, VIDIOC_QUERYCAP, &cap) == 0 && (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) && (cap.device_caps & V4L2_CAP_STREAMING))
            fd = f;
        else
            close(f);
    }
    const auto fail = [&](const char* why) {
        {
            std::lock_guard lock(g_camMutex);
            g_camError = why;
        }
        if (fd >= 0) close(fd);
        g_camRun = false;
        g_idle_add([](gpointer) -> gboolean { if (g_onFrame) g_onFrame(); return G_SOURCE_REMOVE; }, nullptr);
    };
    if (fd < 0) return fail("No camera found");
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (Xioctl(fd, VIDIOC_S_FMT, &fmt) != 0 || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (Xioctl(fd, VIDIOC_S_FMT, &fmt) != 0 || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) return fail("The camera's format isn't supported");
    }
    const bool jpeg = fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG;
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (Xioctl(fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) return fail("The camera is busy");
    struct Buf { void* p; size_t len; };
    std::vector<Buf> bufs;
    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (Xioctl(fd, VIDIOC_QUERYBUF, &b) != 0) break;
        void* p = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (p == MAP_FAILED) break;
        bufs.push_back({p, b.length});
        Xioctl(fd, VIDIOC_QBUF, &b);
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (bufs.size() < 2 || Xioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        for (const Buf& b : bufs) munmap(b.p, b.len);
        return fail("The camera is busy");
    }
    while (g_camRun) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeval tv{0, 200000};
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (Xioctl(fd, VIDIOC_DQBUF, &b) != 0) continue;
        const uint8_t* data = static_cast<const uint8_t*>(bufs[b.index].p);
        if (jpeg) FromJpeg(data, b.bytesused);
        else FromYuyv(data, static_cast<int>(fmt.fmt.pix.width), static_cast<int>(fmt.fmt.pix.height),
                      static_cast<int>(fmt.fmt.pix.bytesperline));
        Xioctl(fd, VIDIOC_QBUF, &b);
    }
    Xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (const Buf& b : bufs) munmap(b.p, b.len);
    close(fd);
}
}  // namespace

bool CameraStart(std::function<void()> frame) {
    if (g_camRun) return true;
    if (g_camThread.joinable()) g_camThread.join();
    g_onFrame = std::move(frame);
    {
        std::lock_guard lock(g_camMutex);
        g_camError.clear();
    }
    g_camRun = true;
    g_camThread = std::thread(CameraLoop);
    return true;
}

void CameraStop() {
    g_camRun = false;
    if (g_camThread.joinable()) g_camThread.join();
    std::lock_guard lock(g_camMutex);
    g_rgb.clear();
    g_rgbW = g_rgbH = 0;
    if (g_surface) {
        cairo_surface_destroy(g_surface);
        g_surface = nullptr;
    }
}

bool CameraRunning() { return g_camRun; }

std::string CameraError() {
    std::lock_guard lock(g_camMutex);
    return g_camError;
}

cairo_surface_t* CameraFrame() {
    std::lock_guard lock(g_camMutex);
    if (g_rgbNew && g_rgbW > 0) {
        g_rgbNew = false;
        if (g_surface && (cairo_image_surface_get_width(g_surface) != g_rgbW || cairo_image_surface_get_height(g_surface) != g_rgbH)) {
            cairo_surface_destroy(g_surface);
            g_surface = nullptr;
        }
        if (!g_surface) g_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, g_rgbW, g_rgbH);
        cairo_surface_flush(g_surface);
        unsigned char* dst = cairo_image_surface_get_data(g_surface);
        const int stride = cairo_image_surface_get_stride(g_surface);
        for (int y = 0; y < g_rgbH; ++y)
            memcpy(dst + y * stride, g_rgb.data() + static_cast<size_t>(y * g_rgbW), static_cast<size_t>(g_rgbW) * 4);
        cairo_surface_mark_dirty(g_surface);
    }
    return g_camRun ? g_surface : nullptr;
}

}  // namespace bb::lshelf
