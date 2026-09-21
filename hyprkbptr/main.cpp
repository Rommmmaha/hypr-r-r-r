#define WLR_USE_UNSTABLE
#include <linux/input-event-codes.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cmath>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "globals.hpp"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
// ============================================================================
// Tuning (hardcoded)
// ============================================================================
constexpr double MOVE_BASE_SPEED = 40.0;  // px/s when a key is first pressed
constexpr double MOVE_ACCEL = 2800.0;      // px/s^2 while held
constexpr double MOVE_MAX_SPEED = 2500.0;  // px/s cap
constexpr double WHEEL_DELTA = 15.0;       // smooth scroll units per notch
constexpr int32_t WHEEL_DISCRETE = 120;    // v120 discrete units per notch
constexpr auto TICK_STEP = std::chrono::milliseconds(8);
// Arrow indices: 0=Up 1=Down 2=Left 3=Right
struct SArrowState {
  bool held = false;
  Time::steady_tp pressTime{};
};
static bool g_active = false;
static std::array<SArrowState, 4> g_arrows;
static bool g_btnLeftHeld = false;
static bool g_btnRightHeld = false;
static bool g_wheelUpHeld = false;
static bool g_wheelDownHeld = false;
static Time::steady_tp g_lastMove{};
static SP<CEventLoopTimer> g_timer = nullptr;
static CHyprSignalListener g_renderListener;
inline CFunctionHook* g_pKeyboardHook = nullptr;
// ============================================================================
// Helpers
// ============================================================================
APICALL EXPORT std::string PLUGIN_API_VERSION() {
  return HYPRLAND_API_VERSION;
}
static uint32_t nowMs() {
  return static_cast<uint32_t>(Time::millis(Time::steadyNow()));
}
// Returns arrow index 0..3, or -1 when the key is not an arrow key.
// Uses +8: Hyprland keycodes are kernel codes, xkb keycodes are offset by 8.
static int keysymToArrow(SP<IKeyboard> kb, uint32_t keycode) {
  if (!kb || !kb->m_xkbState)
    return -1;
  const xkb_keysym_t sym = xkb_state_key_get_one_sym(kb->m_xkbState, keycode + 8);
  switch (sym) {
    case XKB_KEY_Up:
    case XKB_KEY_KP_Up:
      return 0;
    case XKB_KEY_Down:
    case XKB_KEY_KP_Down:
      return 1;
    case XKB_KEY_Left:
    case XKB_KEY_KP_Left:
      return 2;
    case XKB_KEY_Right:
    case XKB_KEY_KP_Right:
      return 3;
    default:
      return -1;
  }
}
static void damageAllMonitors() {
  for (const auto& m : State::monitorState()->monitors()) {
    if (m)
      g_pHyprRenderer->damageMonitor(m);
  }
}
static void pressButton(uint32_t button, bool& heldFlag) {
  if (heldFlag)
    return;
  heldFlag = true;
  IPointer::SButtonEvent ev;
  ev.timeMs = nowMs();
  ev.button = button;
  ev.state = WL_POINTER_BUTTON_STATE_PRESSED;
  ev.mouse = true;
  g_pInputManager->onMouseButton(ev, nullptr);
}
static void releaseButton(uint32_t button, bool& heldFlag) {
  if (!heldFlag)
    return;
  heldFlag = false;
  IPointer::SButtonEvent ev;
  ev.timeMs = nowMs();
  ev.button = button;
  ev.state = WL_POINTER_BUTTON_STATE_RELEASED;
  ev.mouse = true;
  g_pInputManager->onMouseButton(ev, nullptr);
}
// Returns mouse button code for click hotkeys, or 0 when the key is not one.
// PageUp -> left button, PageDown -> right button (hold-to-drag).
static uint32_t keysymToButton(SP<IKeyboard> kb, uint32_t keycode) {
  if (!kb || !kb->m_xkbState)
    return 0;
  const xkb_keysym_t sym = xkb_state_key_get_one_sym(kb->m_xkbState, keycode + 8);
  switch (sym) {
    case XKB_KEY_Page_Up:
    case XKB_KEY_KP_Page_Up:
      return BTN_LEFT;
    case XKB_KEY_Page_Down:
    case XKB_KEY_KP_Page_Down:
      return BTN_RIGHT;
    default:
      return 0;
  }
}
// Returns -1 for scroll-up, +1 for scroll-down, 0 when not a wheel key.
// Home -> wheel up, End -> wheel down (single notch per press).
static int keysymToWheel(SP<IKeyboard> kb, uint32_t keycode) {
  if (!kb || !kb->m_xkbState)
    return 0;
  const xkb_keysym_t sym = xkb_state_key_get_one_sym(kb->m_xkbState, keycode + 8);
  switch (sym) {
    case XKB_KEY_Home:
    case XKB_KEY_KP_Home:
      return -1;
    case XKB_KEY_End:
    case XKB_KEY_KP_End:
      return 1;
    default:
      return 0;
  }
}
static void sendWheel(int direction) {
  IPointer::SAxisEvent ev;
  ev.timeMs = nowMs();
  ev.source = WL_POINTER_AXIS_SOURCE_WHEEL;
  ev.axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
  ev.relativeDirection = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;
  ev.delta = (direction < 0 ? -WHEEL_DELTA : WHEEL_DELTA);
  ev.deltaDiscrete = (direction < 0 ? -WHEEL_DISCRETE : WHEEL_DISCRETE);
  ev.mouse = true;
  g_pInputManager->onMouseWheel(ev, nullptr);
}
static void setActive(bool on) {
  if (on == g_active)
    return;
  g_active = on;
  const auto now = Time::steadyNow();
  if (on) {
    for (auto& a : g_arrows)
      a = SArrowState{};
    g_wheelUpHeld = false;
    g_wheelDownHeld = false;
    g_lastMove = now;
    if (g_timer)
      g_timer->updateTimeout(TICK_STEP);
  } else {
    releaseButton(BTN_LEFT, g_btnLeftHeld);
    releaseButton(BTN_RIGHT, g_btnRightHeld);
    g_wheelUpHeld = false;
    g_wheelDownHeld = false;
    for (auto& a : g_arrows)
      a = SArrowState{};
    if (g_timer)
      g_timer->updateTimeout(std::nullopt);
  }
  damageAllMonitors();
}
// ============================================================================
// Movement timer (main thread, via Hyprland event loop)
// ============================================================================
static void onTick() {
  if (!g_active)
    return;
  if (g_timer)
    g_timer->updateTimeout(TICK_STEP);
  const auto now = Time::steadyNow();
  double dx = 0.0, dy = 0.0;
  double holdSecs = 0.0;
  if (g_arrows[0].held) {
    dy -= 1.0;
    holdSecs = std::max(holdSecs, std::chrono::duration<double>(now - g_arrows[0].pressTime).count());
  }
  if (g_arrows[1].held) {
    dy += 1.0;
    holdSecs = std::max(holdSecs, std::chrono::duration<double>(now - g_arrows[1].pressTime).count());
  }
  if (g_arrows[2].held) {
    dx -= 1.0;
    holdSecs = std::max(holdSecs, std::chrono::duration<double>(now - g_arrows[2].pressTime).count());
  }
  if (g_arrows[3].held) {
    dx += 1.0;
    holdSecs = std::max(holdSecs, std::chrono::duration<double>(now - g_arrows[3].pressTime).count());
  }
  if (dx == 0.0 && dy == 0.0) {
    g_lastMove = now;
    return;
  }
  if (dx != 0.0 && dy != 0.0) {
    dx *= M_SQRT1_2;
    dy *= M_SQRT1_2;
  }
  const double speed = std::min(MOVE_BASE_SPEED + MOVE_ACCEL * holdSecs, MOVE_MAX_SPEED);
  double dt = std::chrono::duration<double>(now - g_lastMove).count();
  dt = std::clamp(dt, 0.0, 0.05);
  g_lastMove = now;
  // Route through the full input pipeline (like a real mouse) so clients get
  // wl_pointer.motion, focus follows, and relative-pointer works.
  // onMouseMoved moves the cursor itself via Pointer::mgr()->move.
  const Vector2D delta{dx * speed * dt, dy * speed * dt};
  IPointer::SMotionEvent motionEv;
  motionEv.timeMs = nowMs();
  motionEv.delta = delta;
  motionEv.unaccel = delta;
  motionEv.mouse = true;
  motionEv.device = nullptr;  // synthetic: onMouseMoved null-checks device
  g_pInputManager->onMouseMoved(motionEv);
  damageAllMonitors();
}
// ============================================================================
// Keyboard hook while active: arrows move, PageUp/PageDown click,
// Home/End send one wheel notch per press, everything else passes through.
// While Super is held hyprkbptr stands down entirely so keybinds keep working.
// ============================================================================
using FnOnKeyboardKey = void (*)(CInputManager*, const IKeyboard::SKeyEvent&, SP<IKeyboard>);
void hkOnKeyboardKey(CInputManager* mgr, const IKeyboard::SKeyEvent& ev, SP<IKeyboard> kb) {
  auto callOriginal = [&] { reinterpret_cast<FnOnKeyboardKey>(g_pKeyboardHook->m_original)(mgr, ev, kb); };
  if (!g_active) {
    callOriginal();
    return;
  }
  if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    // Releases always clear tracked state (avoids stuck keys when Super was
    // involved) and pass through; apps never saw the swallowed press.
    const int releasedArrow = keysymToArrow(kb, ev.keycode);
    if (releasedArrow >= 0)
      g_arrows[releasedArrow] = SArrowState{};
    const uint32_t releasedBtn = keysymToButton(kb, ev.keycode);
    if (releasedBtn == BTN_LEFT)
      releaseButton(BTN_LEFT, g_btnLeftHeld);
    else if (releasedBtn == BTN_RIGHT)
      releaseButton(BTN_RIGHT, g_btnRightHeld);
    const int releasedWheel = keysymToWheel(kb, ev.keycode);
    if (releasedWheel < 0)
      g_wheelUpHeld = false;
    else if (releasedWheel > 0)
      g_wheelDownHeld = false;
    callOriginal();
    return;
  }
  // Stands down while Super is held so Super-based keybinds are untouched.
  const bool super = kb && (kb->getModifiers() & HL_MODIFIER_META);
  if (super) {
    callOriginal();
    return;
  }
  const int arrow = keysymToArrow(kb, ev.keycode);
  if (arrow >= 0) {
    auto& a = g_arrows[arrow];
    if (!a.held) {
      a.held = true;
      a.pressTime = Time::steadyNow();
    }
    return;  // swallowed
  }
  const uint32_t btn = keysymToButton(kb, ev.keycode);
  if (btn == BTN_LEFT) {
    pressButton(BTN_LEFT, g_btnLeftHeld);
    return;  // swallowed
  }
  if (btn == BTN_RIGHT) {
    pressButton(BTN_RIGHT, g_btnRightHeld);
    return;  // swallowed
  }
  // Home/End fire a single wheel notch per physical press: repeats while held
  // are swallowed without re-firing.
  const int wheel = keysymToWheel(kb, ev.keycode);
  if (wheel < 0) {
    if (!g_wheelUpHeld) {
      g_wheelUpHeld = true;
      sendWheel(-1);
    }
    return;  // swallowed
  }
  if (wheel > 0) {
    if (!g_wheelDownHeld) {
      g_wheelDownHeld = true;
      sendWheel(1);
    }
    return;  // swallowed
  }
  callOriginal();
}
// ============================================================================
// Overlay: fullscreen crosshair lines + center cross at cursor
// ============================================================================
static void onRenderStage(eRenderStage stage) {
  if (stage != RENDER_LAST_MOMENT || !g_active)
    return;
  const PHLMONITOR mon = g_pHyprRenderer->renderData().pMonitor.lock();
  if (!mon)
    return;
  const Vector2D mpos = mon->position();
  const Vector2D msize = mon->size();
  const Vector2D cur = Pointer::mgr()->position() - mpos;
  if (cur.x < 0 || cur.y < 0 || cur.x >= msize.x || cur.y >= msize.y)
    return;  // cursor is on another monitor
  const CHyprColor col(1.F, 1.F, 1.F, 0.5F);
  const double t = 1.0;
  g_pHyprRenderer->draw(CRectPassElement::SRectData{.box = CBox{0, std::floor(cur.y), msize.x, t}, .color = col});
  g_pHyprRenderer->draw(CRectPassElement::SRectData{.box = CBox{std::floor(cur.x), 0, t, msize.y}, .color = col});
  g_pHyprRenderer->draw(CRectPassElement::SRectData{.box = CBox{std::floor(cur.x) - 12, std::floor(cur.y) - 1, 25, 3}, .color = col});
  g_pHyprRenderer->draw(CRectPassElement::SRectData{.box = CBox{std::floor(cur.x) - 1, std::floor(cur.y) - 12, 3, 25}, .color = col});
}
// ============================================================================
// Toggle entry points
// ============================================================================
SDispatchResult toggleDispatcher(std::string) {
  setActive(!g_active);
  return SDispatchResult{};
}
int toggleLua(lua_State* L) {
  setActive(!g_active);
  lua_pushboolean(L, g_active);
  return 1;
}
int enableLua(lua_State* L) {
  setActive(true);
  return 0;
}
int disableLua(lua_State* L) {
  setActive(false);
  return 0;
}
// ============================================================================
// Plugin lifecycle
// ============================================================================
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;
  if (std::string(__hyprland_api_get_hash()) != __hyprland_api_get_client_hash())
    throw std::runtime_error("[hyprkbptr] Version mismatch");
  for (const auto& fn : HyprlandAPI::findFunctionsByName(PHANDLE, "onKeyboardKey")) {
    if (fn.demangled.contains("CInputManager")) {
      g_pKeyboardHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)::hkOnKeyboardKey);
      break;
    }
  }
  if (!g_pKeyboardHook || !g_pKeyboardHook->hook())
    throw std::runtime_error("[hyprkbptr] Failed to hook onKeyboardKey");
  g_renderListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) { onRenderStage(stage); });
  g_timer = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) { onTick(); }, nullptr);
  g_pEventLoopManager->addTimer(g_timer);
  HyprlandAPI::addDispatcherV2(PHANDLE, "hyprkbptr:toggle", ::toggleDispatcher);
  HyprlandAPI::addLuaFunction(PHANDLE, "hyprkbptr", "toggle", ::toggleLua);
  HyprlandAPI::addLuaFunction(PHANDLE, "hyprkbptr", "enable", ::enableLua);
  HyprlandAPI::addLuaFunction(PHANDLE, "hyprkbptr", "disable", ::disableLua);
  return {"hyprkbptr", "Drive the mouse with arrow keys, with crosshair overlay", "Rommmmaha", "1.0"};
}
APICALL EXPORT void PLUGIN_EXIT() {
  // Tear everything down BEFORE dlclose: any surviving callback (render
  // listener, event-loop timer) would jump into unmapped memory on the next
  // frame/tick and take the compositor down.
  setActive(false);
  if (g_timer) {
    g_timer->cancel();
    g_pEventLoopManager->removeTimer(g_timer);
    g_timer.reset();
  }
  g_renderListener.reset();
}
