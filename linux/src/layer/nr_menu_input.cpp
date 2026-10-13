#include "nr_menu.hpp"
#include "imgui.h"
#include <xcb/xcb.h>
#include <X11/keysym.h>
#include <array>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>

namespace nr {
namespace {
// ---- the Wayland input path -------------------------------------------------
//
// A Vulkan layer cannot read the keyboard on Wayland; the protocol does not
// offer it, and that is deliberate rather than an omission. The only source is
// the application, so the SDL3 bridge hands its events over and this side turns
// them into ImGui input. Games that do not use SDL get no overlay on a native
// Wayland surface, and the menu says so rather than appearing and ignoring keys.
//
// Under Proton the game renders through XWayland, which is an X11 surface, so
// the validated xcb path below applies unchanged - this is only for native
// Wayland clients.
struct Observed {
    uint32_t kind;    // 1 key, 2 motion, 3 button, 4 wheel, 5 text
    int32_t a, b;
    uint32_t mods;
    uint32_t text;
};
// USB HID usage codes, which is what SDL scancodes are. Spelled out here so the
// layer does not have to link SDL to talk to the bridge.
enum : int32_t {
    kScanA = 4, kScanZ = 29, kScan1 = 30, kScan0 = 39,
    kScanReturn = 40, kScanEscape = 41, kScanBackspace = 42, kScanTab = 43,
    kScanSpace = 44, kScanMinus = 45, kScanEquals = 46,
    kScanF1 = 58, kScanF10 = 67,
    kScanInsert = 73, kScanHome = 74, kScanPageUp = 75, kScanDelete = 76,
    kScanEnd = 77, kScanPageDown = 78,
    kScanRight = 79, kScanLeft = 80, kScanDown = 81, kScanUp = 82,
    kScanKpEnter = 88, kScanKp1 = 89, kScanKp0 = 98, kScanKpPeriod = 99,
    kScanLCtrl = 224, kScanLShift = 225, kScanLAlt = 226,
    kScanRCtrl = 228, kScanRShift = 229, kScanRAlt = 230,
};
ImGuiKey from_scancode(int32_t code) {
    if (code >= kScanA && code <= kScanZ) return ImGuiKey(ImGuiKey_A + (code - kScanA));
    if (code >= kScan1 && code <= kScan0 - 1) return ImGuiKey(ImGuiKey_1 + (code - kScan1));
    if (code == kScan0) return ImGuiKey_0;
    if (code >= kScanKp1 && code <= kScanKp0 - 1) return ImGuiKey(ImGuiKey_Keypad1 + (code - kScanKp1));
    if (code >= kScanF1 && code <= kScanF1 + 11) return ImGuiKey(ImGuiKey_F1 + (code - kScanF1));
    switch (code) {
    case kScanReturn: case kScanKpEnter: return ImGuiKey_Enter;
    case kScanEscape: return ImGuiKey_Escape;
    case kScanBackspace: return ImGuiKey_Backspace;
    case kScanTab: return ImGuiKey_Tab;
    case kScanSpace: return ImGuiKey_Space;
    case kScanMinus: return ImGuiKey_Minus;
    case kScanEquals: return ImGuiKey_Equal;
    case kScanInsert: return ImGuiKey_Insert;
    case kScanHome: return ImGuiKey_Home;
    case kScanEnd: return ImGuiKey_End;
    case kScanPageUp: return ImGuiKey_PageUp;
    case kScanPageDown: return ImGuiKey_PageDown;
    case kScanDelete: return ImGuiKey_Delete;
    case kScanLeft: return ImGuiKey_LeftArrow;
    case kScanRight: return ImGuiKey_RightArrow;
    case kScanUp: return ImGuiKey_UpArrow;
    case kScanDown: return ImGuiKey_DownArrow;
    case kScanKp0: return ImGuiKey_Keypad0;
    case kScanKpPeriod: return ImGuiKey_KeypadDecimal;
    case kScanLCtrl: return ImGuiKey_LeftCtrl;
    case kScanRCtrl: return ImGuiKey_RightCtrl;
    case kScanLShift: return ImGuiKey_LeftShift;
    case kScanRShift: return ImGuiKey_RightShift;
    case kScanLAlt: return ImGuiKey_LeftAlt;
    case kScanRAlt: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey named(uint32_t s) {
    if (s >= XK_A && s <= XK_Z)
        s += XK_a - XK_A;
    if (s >= XK_a && s <= XK_z)
        return ImGuiKey(ImGuiKey_A + s - XK_a);
    if (s >= XK_0 && s <= XK_9)
        return ImGuiKey(ImGuiKey_0 + s - XK_0);
    switch (s) {
    case XK_Tab:
        return ImGuiKey_Tab;
    case XK_Left:
        return ImGuiKey_LeftArrow;
    case XK_Right:
        return ImGuiKey_RightArrow;
    case XK_Up:
        return ImGuiKey_UpArrow;
    case XK_Down:
        return ImGuiKey_DownArrow;
    case XK_Page_Up:
        return ImGuiKey_PageUp;
    case XK_Page_Down:
        return ImGuiKey_PageDown;
    case XK_Home:
        return ImGuiKey_Home;
    case XK_End:
        return ImGuiKey_End;
    case XK_Insert:
        return ImGuiKey_Insert;
    case XK_Delete:
        return ImGuiKey_Delete;
    case XK_BackSpace:
        return ImGuiKey_Backspace;
    case XK_space:
        return ImGuiKey_Space;
    case XK_Return:
    case XK_KP_Enter:
        return ImGuiKey_Enter;
    case XK_Escape:
        return ImGuiKey_Escape;
    case XK_Shift_L:
        return ImGuiKey_LeftShift;
    case XK_Shift_R:
        return ImGuiKey_RightShift;
    case XK_Control_L:
        return ImGuiKey_LeftCtrl;
    case XK_Control_R:
        return ImGuiKey_RightCtrl;
    case XK_Alt_L:
        return ImGuiKey_LeftAlt;
    case XK_Alt_R:
        return ImGuiKey_RightAlt;
    case XK_Super_L:
        return ImGuiKey_LeftSuper;
    case XK_Super_R:
        return ImGuiKey_RightSuper;
    default:
        return ImGuiKey_None;
    }
}
} // namespace
struct MenuInput::Impl {
    using Bridge = int (*)(uint32_t,bool);
    Bridge bridge{};
    xcb_connection_t *connection{};
    using Drain = int (*)(void *, int);
    Drain drain{};
    bool wayland{};
    float mouse_x{}, mouse_y{};
    xcb_window_t window{};
    xcb_cursor_t cursor{};
    std::array<std::array<uint32_t, 2>, 256> symbols{};
    bool visible{}, grabbed{}, insert_down{}, f10_down{}, focus_hint{};
    std::string message;
    void release() {
        if (!connection)
            return;
        if (grabbed) {
            xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
            xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
            xcb_flush(connection);
        }
        grabbed = false;
        if(bridge)bridge(window,false);
    }
    bool focused() {
        if (focus_hint)
            return true;
        auto *reply = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);
        if (!reply)
            return false;
        auto focus = reply->focus;
        free(reply);
        // The swapchain's window inside the focused one (a game drawing into a child window, or Wine's client
        // window under its toplevel): the focus is one of its ancestors below the root.
        for (xcb_window_t w = window, i = 0; i < 16 && w && focus > 1; ++i) {
            if (w == focus)
                return true;
            auto *tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, w), nullptr);
            if (!tree)
                break;
            const xcb_window_t parent = tree->parent, root = tree->root;
            free(tree);
            if (!parent || parent == root)
                break;
            w = parent;
        }
        for (unsigned i = 0; i < 16 && focus > 1; ++i) {
            if (focus == window)
                return true;
            auto *tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, focus), nullptr);
            if (!tree)
                break;
            auto parent = tree->parent;
            free(tree);
            if (parent == focus)
                break;
            focus = parent;
        }
        return false;
    }
};
MenuInput::MenuInput(const std::string &display, uint32_t window) : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    s.window = window;
    s.bridge=reinterpret_cast<Impl::Bridge>(dlsym(RTLD_DEFAULT,"nrSDL3MenuRequest"));
    s.drain=reinterpret_cast<Impl::Drain>(dlsym(RTLD_DEFAULT,"nrSDL3MenuDrain"));
    s.connection = xcb_connect(display.empty() ? nullptr : display.c_str(), nullptr);
    if (!s.connection || xcb_connection_has_error(s.connection)) {
        s.message = "X11 input connection unavailable";
        return;
    }
    auto *setup = xcb_get_setup(s.connection);
    auto *map = xcb_get_keyboard_mapping_reply(
        s.connection,
        xcb_get_keyboard_mapping(s.connection, setup->min_keycode,
                                 uint8_t(setup->max_keycode - setup->min_keycode + 1)),
        nullptr);
    if (map) {
        auto *keys = xcb_get_keyboard_mapping_keysyms(map);
        for (unsigned code = setup->min_keycode; code <= setup->max_keycode; ++code)
            for (unsigned col = 0; col < 2; ++col)
                s.symbols[code][col] = keys[(code - setup->min_keycode) * map->keysyms_per_keycode +
                                            (col < map->keysyms_per_keycode ? col : 0)];
        free(map);
    }
    auto font = xcb_generate_id(s.connection);
    xcb_open_font(s.connection, font, 6, "cursor");
    s.cursor = xcb_generate_id(s.connection);
    xcb_create_glyph_cursor(s.connection, s.cursor, font, font, 68, 69, 0xffff, 0xffff, 0xffff, 0, 0, 0);
    xcb_close_font(s.connection, font);
    xcb_flush(s.connection);
    s.message = "End: menu   F10: toggle NR";
}
void MenuInput::update_bridge(ImGuiIO &io, bool &enabled) {
    auto &s = *impl_;
    if (!s.drain || !s.bridge) {
        s.visible = false;
        return;
    }
    io.AddFocusEvent(true);
    Observed events[256];
    bool insert_edge = false, f10_edge = false;
    int n;
    while ((n = s.drain(events, 256)) > 0) {
        for (int i = 0; i < n; ++i) {
            const Observed &e = events[i];
            switch (e.kind) {
            case 1: {
                // Hotkeys are edges, and they are read whether or not the menu
                // already holds input - the End that opens it arrives while
                // the game still owns the keyboard.
                if (e.a == kScanEnd && e.b) insert_edge = true;
                if (e.a == kScanF10 && e.b) f10_edge = true;
                if (!s.grabbed) break;
                const ImGuiKey key = from_scancode(e.a);
                if (key != ImGuiKey_None) io.AddKeyEvent(key, e.b != 0);
                io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & 0x00c0) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (e.mods & 0x0003) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (e.mods & 0x0300) != 0);
                break;
            }
            case 2:
                s.mouse_x = float(e.a); s.mouse_y = float(e.b);
                if (s.grabbed) io.AddMousePosEvent(s.mouse_x, s.mouse_y);
                break;
            case 3:
                if (s.grabbed && e.a >= 1 && e.a <= 3)
                    io.AddMouseButtonEvent(e.a == 1 ? 0 : e.a == 3 ? 1 : 2, e.b != 0);
                break;
            case 4:
                if (s.grabbed) io.AddMouseWheelEvent(float(e.a), float(e.b));
                break;
            case 5:
                if (s.grabbed && io.WantTextInput && e.text >= 32 && e.text < 127)
                    io.AddInputCharacter(e.text);
                break;
            default: break;
            }
        }
        if (n < 256) break;
    }
    if (insert_edge) s.visible = !s.visible;
    if (f10_edge) enabled = !enabled;

    // Capture follows the same handshake the X11 path uses, minus the grab:
    // there is nothing to grab, so the bridge swallowing the game's input *is*
    // the capture.
    const int status = s.bridge(s.window, s.visible);
    const bool held = status == 2;
    if (held != s.grabbed) {
        s.grabbed = held;
        io.ClearInputKeys();
        if (held) io.AddMousePosEvent(s.mouse_x, s.mouse_y);
    }
    s.message = s.visible ? (s.grabbed ? "End: close   F10: toggle NR"
                                       : status < 0 ? "SDL3 input handoff unavailable"
                                                    : "Waiting for game input thread")
                          : "End: menu   F10: toggle NR";
}

// The Wayland constructor: no display connection, because there is nothing to
// connect to. Input arrives from the application through the SDL3 bridge or not
// at all, and `status()` says which.
MenuInput::MenuInput() : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    s.window = 1;   // the bridge keys on a non-zero token, not on an X window
    s.wayland = true;
    s.bridge = reinterpret_cast<Impl::Bridge>(dlsym(RTLD_DEFAULT, "nrSDL3MenuRequest"));
    s.drain = reinterpret_cast<Impl::Drain>(dlsym(RTLD_DEFAULT, "nrSDL3MenuDrain"));
    s.message = s.bridge && s.drain
        ? "End: menu   F10: toggle NR"
        : "Wayland: overlay input needs the SDL3 bridge (NR_SDL3_INPUT=1)";
}

MenuInput::~MenuInput() {
    auto &s = *impl_;
    s.release();
    if (s.connection) {
        if (s.cursor)
            xcb_free_cursor(s.connection, s.cursor);
        xcb_disconnect(s.connection);
    }
}
bool MenuInput::visible() const { return impl_->visible; }
bool MenuInput::captured() const { return impl_->grabbed; }
const std::string &MenuInput::status() const { return impl_->message; }
void MenuInput::close() { impl_->visible = false; }
void MenuInput::set_focus_hint(bool focused) { impl_->focus_hint = focused; }
void MenuInput::update(ImGuiIO &io, bool &enabled) {
    auto &s = *impl_;
    if (s.wayland) { update_bridge(io, enabled); return; }
    if (!s.connection || xcb_connection_has_error(s.connection))
        return;
    if (!s.focused()) {
        s.visible = false;
        s.release();
        io.ClearInputKeys();
        io.AddFocusEvent(false);
        s.insert_down = s.f10_down = false;
        return;
    }
    io.AddFocusEvent(true);
    auto *keymap = xcb_query_keymap_reply(s.connection, xcb_query_keymap(s.connection), nullptr);
    auto *pointer = xcb_query_pointer_reply(s.connection, xcb_query_pointer(s.connection, s.window), nullptr);
    if (!keymap || !pointer) {
        free(keymap);
        free(pointer);
        s.visible = false;
        s.release();
        return;
    }
    bool any = false, insert = false, f10 = false;
    for (unsigned code = 8; code < 256; ++code) {
        bool down = (uint8_t(keymap->keys[code / 8]) & (1u << (code % 8))) != 0;
        any |= down;
        if (s.symbols[code][0] == XK_End)
            insert = down;
        if (s.symbols[code][0] == XK_F10)
            f10 = down;
    }
    any |= (pointer->mask & (XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3)) != 0;
    // No passive key grab: the game receives releases for keys held before the
    // menu opens. Acquire input only after *all* those keys/buttons are released.
    if (!s.visible || !io.WantTextInput) {
        if (insert && !s.insert_down)
            s.visible = !s.visible;
        if (f10 && !s.f10_down)
            enabled = !enabled;
    }
    if (!s.grabbed) {
        int bridge_state=0;
        if(s.bridge&&s.visible&&!any)bridge_state=s.bridge(s.window,true);
        if (s.visible && !any && (!s.bridge||bridge_state==2)) {
            auto *keyboard =
                xcb_grab_keyboard_reply(s.connection,
                                        xcb_grab_keyboard(s.connection, false, s.window, XCB_CURRENT_TIME,
                                                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC),
                                        nullptr);
            auto *mouse = xcb_grab_pointer_reply(s.connection,
                                                 xcb_grab_pointer(s.connection, false, s.window,
                                                                  XCB_EVENT_MASK_BUTTON_PRESS |
                                                                      XCB_EVENT_MASK_BUTTON_RELEASE |
                                                                      XCB_EVENT_MASK_POINTER_MOTION,
                                                                  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                                                                  XCB_NONE, s.cursor, XCB_CURRENT_TIME),
                                                 nullptr);
            const bool kg = keyboard && keyboard->status == XCB_GRAB_STATUS_SUCCESS,
                       mg = mouse && mouse->status == XCB_GRAB_STATUS_SUCCESS;
            s.grabbed = kg && mg;
            free(keyboard);
            free(mouse);
            if (!s.grabbed) {
                if (kg)
                    xcb_ungrab_keyboard(s.connection, XCB_CURRENT_TIME);
                if (mg)
                    xcb_ungrab_pointer(s.connection, XCB_CURRENT_TIME);
                s.message = "Input unavailable: another client holds a grab";
            } else {
                s.message = "End: close   F10: toggle NR";
                io.ClearInputKeys();
            }
            xcb_flush(s.connection);
        } else if (s.visible)
            s.message = any?"Release held keys/buttons to focus the menu":bridge_state<0?"SDL3 input handoff unavailable":"Waiting for game input thread";
    }
    s.insert_down = insert;
    s.f10_down = f10;
    io.AddMousePosEvent(float(pointer->win_x), float(pointer->win_y));
    const uint16_t modifiers = pointer->mask;
    free(pointer);
    free(keymap);
    std::vector<xcb_generic_event_t *> events;
    while (auto *e = xcb_poll_for_event(s.connection))
        events.push_back(e);
    std::array<bool, 256> text_repeated{};
    for (size_t index = 0; index < events.size(); ++index) {
        auto *event = events[index];
        // Core X11 auto-repeat is a release/press pair with matching key/time.
        // ImGui owns named-key repeat. Do not feed synthetic pairs: after a
        // synchronous pipeline build they can delay real mouse input for seconds.
        if ((event->response_type & 0x7f) == XCB_KEY_RELEASE && index + 1 < events.size() &&
            (events[index + 1]->response_type & 0x7f) == XCB_KEY_PRESS) {
            auto *up = reinterpret_cast<xcb_key_release_event_t *>(event);
            auto *down = reinterpret_cast<xcb_key_press_event_t *>(events[index + 1]);
            if (up->detail == down->detail && up->time == down->time) {
                if (io.WantTextInput && !text_repeated[down->detail] &&
                    !(down->state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1))) {
                    auto ch = s.symbols[down->detail][(down->state & XCB_MOD_MASK_SHIFT) ? 1 : 0];
                    if (ch >= 32 && ch < 127)
                        io.AddInputCharacter(ch);
                    text_repeated[down->detail] = true;
                }
                free(event);
                free(events[++index]);
                continue;
            }
        }
        auto type = event->response_type & 0x7f;
        if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
            auto *key = reinterpret_cast<xcb_key_press_event_t *>(event);
            bool down = type == XCB_KEY_PRESS;
            const uint32_t symbol = s.symbols[key->detail][0];
            auto k = named(symbol);
            if (k != ImGuiKey_None)
                io.AddKeyEvent(k, down);
            io.AddKeyEvent(ImGuiMod_Ctrl, (key->state & XCB_MOD_MASK_CONTROL) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (key->state & XCB_MOD_MASK_SHIFT) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (key->state & XCB_MOD_MASK_1) != 0);
            if (down && !io.WantTextInput && (symbol == XK_Escape))
                s.visible = false;
            if (down && !(key->state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1))) {
                auto ch = s.symbols[key->detail][(key->state & XCB_MOD_MASK_SHIFT) ? 1 : 0];
                if (ch >= 32 && ch < 127)
                    io.AddInputCharacter(ch);
            }
        } else if (type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE) {
            auto *button = reinterpret_cast<xcb_button_press_event_t *>(event);
            bool down = type == XCB_BUTTON_PRESS;
            io.AddMousePosEvent(float(button->event_x), float(button->event_y));
            if (button->detail <= 3)
                io.AddMouseButtonEvent(button->detail == 1 ? 0 : button->detail == 3 ? 1 : 2, down);
            else if (down && (button->detail == 4 || button->detail == 5))
                io.AddMouseWheelEvent(0, button->detail == 4 ? 1.0f : -1.0f);
        }
        free(event);
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & XCB_MOD_MASK_CONTROL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & XCB_MOD_MASK_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & XCB_MOD_MASK_1) != 0);
    // Keep consuming releases for keys pressed while captured. Releasing the
    // grab on a press would send an orphan key-up to the game on menu close.
    if (!s.visible && (s.grabbed || s.bridge) && !any) {
        s.release();
        io.ClearInputKeys();
        s.message = "End: menu   F10: toggle NR";
    }
}
} // namespace nr
