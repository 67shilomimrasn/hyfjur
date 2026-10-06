#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Keybind parsing + sending (Windows)
// ---------------------------------------------------------------------------

#ifdef GEODE_IS_WINDOWS

static std::string trim(std::string s) {
	while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
	while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
	return s;
}

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

// Returns 0 if the key name is unknown.
static WORD keyNameToVk(std::string const& raw) {
	std::string k = lower(raw);

	if (k == "ctrl" || k == "control") return VK_CONTROL;
	if (k == "shift") return VK_SHIFT;
	if (k == "alt") return VK_MENU;

	// F1 - F24
	if (k.size() >= 2 && k[0] == 'f' && isdigit((unsigned char)k[1])) {
		int n = atoi(k.c_str() + 1);
		if (n >= 1 && n <= 24) return (WORD)(VK_F1 + n - 1);
	}

	// Numpad0 - Numpad9
	if (k.rfind("numpad", 0) == 0 && k.size() == 7 && isdigit((unsigned char)k[6])) {
		return (WORD)(VK_NUMPAD0 + (k[6] - '0'));
	}

	if (k == "space") return VK_SPACE;
	if (k == "enter" || k == "return") return VK_RETURN;
	if (k == "tab") return VK_TAB;
	if (k == "escape" || k == "esc") return VK_ESCAPE;
	if (k == "backspace") return VK_BACK;
	if (k == "insert") return VK_INSERT;
	if (k == "delete" || k == "del") return VK_DELETE;
	if (k == "home") return VK_HOME;
	if (k == "end") return VK_END;
	if (k == "pageup") return VK_PRIOR;
	if (k == "pagedown") return VK_NEXT;
	if (k == "up") return VK_UP;
	if (k == "down") return VK_DOWN;
	if (k == "left") return VK_LEFT;
	if (k == "right") return VK_RIGHT;

	// single letter / digit
	if (k.size() == 1) {
		char c = k[0];
		if (c >= 'a' && c <= 'z') return (WORD)('A' + (c - 'a'));
		if (c >= '0' && c <= '9') return (WORD)c;
		SHORT vk = VkKeyScanA(c);
		if (vk != -1) return (WORD)(vk & 0xFF);
	}
	return 0;
}

static std::vector<WORD> parseKeybind(std::string const& text) {
	std::vector<WORD> keys;
	size_t start = 0;
	while (start <= text.size()) {
		size_t plus = text.find('+', start);
		std::string part = trim(text.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
		if (!part.empty()) {
			WORD vk = keyNameToVk(part);
			if (vk == 0) return {};
			keys.push_back(vk);
		}
		if (plus == std::string::npos) break;
		start = plus + 1;
	}
	return keys;
}

static INPUT makeKey(WORD vk, bool up) {
	INPUT in{};
	in.type = INPUT_KEYBOARD;
	in.ki.wVk = vk;
	in.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);

	// Extended keys need the extended flag when sent by scancode
	switch (vk) {
		case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
		case VK_PRIOR: case VK_NEXT: case VK_UP: case VK_DOWN:
		case VK_LEFT: case VK_RIGHT:
			in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			break;
		default: break;
	}
	return in;
}

static void pressKeybind(std::vector<WORD> keys) {
	// Done on a separate thread so the game never stutters.
	std::thread([keys = std::move(keys)]() {
		using namespace std::chrono_literals;
		for (WORD vk : keys) {
			INPUT in = makeKey(vk, false);
			SendInput(1, &in, sizeof(INPUT));
			std::this_thread::sleep_for(15ms);
		}
		std::this_thread::sleep_for(40ms);
		for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
			INPUT in = makeKey(*it, true);
			SendInput(1, &in, sizeof(INPUT));
			std::this_thread::sleep_for(15ms);
		}
	}).detach();
}

static bool sendConfiguredKeybind() {
	auto text = Mod::get()->getSettingValue<std::string>("keybind");
	auto keys = parseKeybind(text);
	if (keys.empty()) {
		log::warn("Auto Deafen: could not parse keybind '{}'", text);
		return false;
	}
	pressKeybind(std::move(keys));
	return true;
}

#else
static bool sendConfiguredKeybind() {
	log::warn("Auto Deafen only supports Windows");
	return false;
}
#endif

// ---------------------------------------------------------------------------
// PlayLayer hook
// ---------------------------------------------------------------------------

class $modify(AutoDeafenPlayLayer, PlayLayer) {
	struct Fields {
		bool m_deafenedByMod = false;
	};

	void tryUndeafen() {
		auto f = m_fields.self();
		if (!f->m_deafenedByMod) return;
		f->m_deafenedByMod = false;
		if (Mod::get()->getSettingValue<bool>("undeafen")) {
			sendConfiguredKeybind();
		}
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		auto mod = Mod::get();
		if (!mod->getSettingValue<bool>("enabled")) return;
		if (m_isPracticeMode && !mod->getSettingValue<bool>("practice")) return;

		auto f = m_fields.self();
		if (f->m_deafenedByMod) return;
		if (m_player1 && m_player1->m_isDead) return;

		float target = static_cast<float>(mod->getSettingValue<int64_t>("target-percent"));
		if (this->getCurrentPercent() >= target) {
			if (sendConfiguredKeybind()) {
				f->m_deafenedByMod = true;
			}
		}
	}

	// New attempt (after death or restart)
	void resetLevel() {
		tryUndeafen();
		PlayLayer::resetLevel();
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		tryUndeafen();
	}

	void onQuit() {
		tryUndeafen();
		PlayLayer::onQuit();
	}
};
