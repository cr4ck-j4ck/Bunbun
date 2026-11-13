// Bunbun: File Tray drag and drop (target, source, drag start).
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

class TrayDropTarget final : public IDropTarget {
   public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDropTarget))) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = --refs_;
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }

    STDMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        accept_ = false;
        if (!g_trayInternalDrag.load() && TrayModuleEnabled() && data) {
            FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            accept_ = (data->QueryGetData(&fmt) == S_OK);
        }

        if (accept_) {
            tab_ = FileTrayTabIndex(GetSettingsCopy());
            if (tab_ >= 0) {
                g_idleTab = tab_;
            }
            g_trayDragOver = true;
            g_clickExpanded = true;
            g_layoutDirty = true;
            TriggerNudge();
        }
        if (effect) {
            *effect = accept_ ? PickTrayDropEffect(*effect) : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) override {
        if (accept_ && tab_ >= 0) {
            g_idleTab = tab_;  // stay on the tray while the island expands
        }
        if (effect) {
            *effect = accept_ ? PickTrayDropEffect(*effect) : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    STDMETHODIMP DragLeave() override {
        accept_ = false;
        g_trayDragOver = false;
        g_layoutDirty = true;
        return S_OK;
    }

    STDMETHODIMP Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        std::vector<std::wstring> paths;
        const bool ok = accept_ && ExtractDropPaths(data, &paths);
        accept_ = false;

        if (ok) {
            AddPathsToFileTray(paths);
            NoteTrayDrop(paths);  // Bunbun: the Studio frame's "caught it" card
            const int tab = FileTrayTabIndex(GetSettingsCopy());
            if (tab >= 0) {
                g_idleTab = tab;
            }
            g_clickExpanded = true;
            g_layoutDirty = true;
            TriggerNudge();
            if (effect) *effect = PickTrayDropEffect(*effect);
        } else if (effect) {
            *effect = DROPEFFECT_NONE;
        }
        g_trayDragOver = false;
        return S_OK;
    }

   private:
    std::atomic<LONG> refs_{1};
    bool accept_ = false;
    int tab_ = -1;
};

class TrayDropSource final : public IDropSource {
   public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDropSource))) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = --refs_;
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    STDMETHODIMP QueryContinueDrag(BOOL escPressed, DWORD keyState) override {
        if (escPressed) return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

   private:
    std::atomic<LONG> refs_{1};
};

// Builds the same data object Explorer would hand out for this file, so any
// app, folder or the desktop accepts it as a normal file drag.
bool CreateFileDataObject(const std::wstring& path, IDataObject** out) {
    *out = nullptr;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) {
        return false;
    }

    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child);
    if (SUCCEEDED(hr)) {
        hr = parent->GetUIObjectOf(nullptr, 1, &child, __uuidof(IDataObject), nullptr,
                                   reinterpret_cast<void**>(out));
    }
    CoTaskMemFree(pidl);
    return SUCCEEDED(hr) && *out != nullptr;
}

// Blocks (modal OLE loop) until the drag finishes.
void StartFileTrayDrag(const std::wstring& path) {
    ComPtr<IDataObject> dataObject;
    if (!CreateFileDataObject(path, dataObject.GetAddressOf())) {
        return;
    }

    ComPtr<IDropSource> source;
    source.Attach(new TrayDropSource());

    constexpr DWORD kOkEffects = DROPEFFECT_COPY | DROPEFFECT_LINK;
    DWORD effect = DROPEFFECT_NONE;

    // SHDoDragDrop adds the normal Explorer drag image (thumbnail + name).
    // Resolved dynamically; plain DoDragDrop is the fallback.
    using SHDoDragDrop_t = HRESULT(WINAPI*)(HWND, IDataObject*, IDropSource*, DWORD, DWORD*);
    static const auto pSHDoDragDrop = reinterpret_cast<SHDoDragDrop_t>(
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHDoDragDrop"));

    g_trayInternalDrag = true;
    if (pSHDoDragDrop) {
        pSHDoDragDrop(nullptr, dataObject.Get(), source.Get(), kOkEffects, &effect);
    } else {
        DoDragDrop(dataObject.Get(), source.Get(), kOkEffects, &effect);
    }
    g_trayInternalDrag = false;

    g_hoveredFileTrayRow = -1;
    g_hoveredFileTrayAction = -1;
    g_layoutDirty = true;

    // File was moved away by the target: its tray entry is now stale.
    if (effect != DROPEFFECT_NONE && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        RemoveFileTrayItemByPath(path);
    }
}

