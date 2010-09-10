/* Copyright 2010 Ilkka Halila
This file is part of Goblin Camp.

Goblin Camp is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Goblin Camp is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License 
along with Goblin Camp. If not, see <http://www.gnu.org/licenses/>.*/
#include "stdafx.hpp"

#include <libtcod.hpp>
#define BOOST_FILESYSTEM_VERSION 3
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>

#include <cassert>
#include <string>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <map>

namespace fs = boost::filesystem;

#include "Data.hpp"
#include "Game.hpp"
#include "Logger.hpp"
#include "Item.hpp"
#include "NatureObject.hpp"
#include "NPC.hpp"
#include "Construction.hpp"
#include "UI.hpp"
#include "UI/YesNoDialog.hpp"
#include "scripting/Engine.hpp"
#include "scripting/Event.hpp"

// These functions are platform-specific, and are defined in <platform>/DataImpl.cpp.
void _ImplFindPersonalDirectory(std::string&);
void _ImplFindExecutableDirectory(fs::path&, fs::path&, fs::path&);

namespace {
	namespace globals {
		fs::path personalDir, exec, execDir, dataDir;
		fs::path savesDir, screensDir, modsDir;
		fs::path config, defaultKeys, keys, font;
		
		std::list<Data::Mod> loadedMods;
	}
	
	// Finds path to 'personal dir' and subdirs.
	inline void FindPersonalDirectory() {
		std::string dir;
		
		_ImplFindPersonalDirectory(dir);
		
		globals::personalDir = fs::path(dir);
		globals::savesDir    = globals::personalDir / "saves";
		globals::screensDir  = globals::personalDir / "screenshots";
		globals::config      = globals::personalDir / "config.ini";
		globals::keys        = globals::personalDir / "keys.ini";
		globals::font        = globals::personalDir / "terminal.png";
		globals::modsDir     = globals::personalDir / "mods";
	}
	
	// Finds 'executable', 'bin-dir' and 'global data'.
	inline void FindExecutableDirectory() {
		_ImplFindExecutableDirectory(globals::exec, globals::execDir, globals::dataDir);
		globals::defaultKeys = globals::dataDir / "keys.ini";
		if(!fs::exists(globals::defaultKeys)) {
			globals::defaultKeys = globals::execDir / "keys.ini";
		}
	}
	
	void _LoadNames(std::string fn) {
		TCODNamegen::parse(fn.c_str());
	}
	
	void _TryLoadDataFile(
		bool global, const fs::path& dir1, const fs::path& dir2,
		const std::string& filename, void (*load)(std::string)
	) {
		std::string fn = filename + ".dat";
		fs::path dir;
		
		Logger::Inst()->output << "[Data] Searching for: " << fn << "\n";
		
		if (fs::exists(dir1 / fn)) {
			dir = dir1;
		} else if (global) {
			if (fs::exists(dir2 / fn)) {
				dir = dir2;
			} else {
				Logger::Inst()->output << "[Data] Cannot find global data file -- abort.\n";
				Logger::Inst()->output.flush();
				exit(5);
			}
		} else {
			Logger::Inst()->output << "[Data] Not found, skipping.\n";
			return;
		}
		
		Logger::Inst()->output << "[Data] Loading: " << (dir / fn).string() << "\n";
		load((dir / fn).string());
	}
	
	inline void TryLoadGlobalDataFile(const std::string& filename, void (*load)(std::string)) {
		_TryLoadDataFile(true, globals::dataDir, globals::execDir, filename, load);
	}
	
	inline void TryLoadLocalDataFile(const fs::path& dir, const std::string& filename, void (*load)(std::string)) {
		_TryLoadDataFile(false, dir, dir, filename, load);
	}
	
	void LoadGlobalMod() {
		Logger::Inst()->output << "[Data] Loading global data files.\n";
		
		TryLoadGlobalDataFile("items",         Item::LoadPresets);
		TryLoadGlobalDataFile("constructions", Construction::LoadPresets);
		TryLoadGlobalDataFile("wildplants",    NatureObject::LoadPresets);
		TryLoadGlobalDataFile("names",         _LoadNames);
		TryLoadGlobalDataFile("creatures",     NPC::LoadPresets);
	}
	
	struct ModListener : public ITCODParserListener {
		Data::Mod *ptr;
		
		ModListener(Data::Mod *ptr) : ptr(ptr) {
			assert(ptr != NULL);
		}
		
		bool parserProperty(TCODParser*, const char *name, TCOD_value_type_t type, TCOD_value_t value) {
			if (boost::iequals(name, "name")) {
				ptr->name = value.s;
			} else if (boost::iequals(name, "author")) {
				ptr->author = value.s;
			} else if (boost::iequals(name, "version")) {
				ptr->version = value.s;
			} else if (boost::iequals(name, "apiversion")) {
				ptr->apiVersion = value.i;
			}
			
			return true;
		}
		
		void error(const char *err) {
			Logger::Inst()->output << "ModListener: " << err << "\n";
		}
		
		// unused
		bool parserNewStruct(TCODParser*, const TCODParserStruct*, const char*) { return true; }
		bool parserFlag(TCODParser*, const char*) { return true; }
		bool parserEndStruct(TCODParser*, const TCODParserStruct*, const char*) { return true; }
	};
	
	void LoadLocalMods() {
		fs::directory_iterator end;
		
		TCODParser parser;
		TCODParserStruct *type = parser.newStructure("mod");
		type->addProperty("name",       TCOD_TYPE_STRING, true);
		type->addProperty("author",     TCOD_TYPE_STRING, true);
		type->addProperty("version",    TCOD_TYPE_STRING, true);
		type->addProperty("apiVersion", TCOD_TYPE_INT, false);
		
		for (fs::directory_iterator it(globals::modsDir); it != end; ++it) {
			if (!fs::is_directory(it->status())) continue;
			
			fs::path mod = it->path();
			Logger::Inst()->output << "[Data] Loading mod: " << mod.filename() << "\n";
			
			TryLoadLocalDataFile(mod, "items",         Item::LoadPresets);
			TryLoadLocalDataFile(mod, "constructions", Construction::LoadPresets);
			TryLoadLocalDataFile(mod, "wildplants",    NatureObject::LoadPresets);
			TryLoadLocalDataFile(mod, "names",         _LoadNames);
			TryLoadLocalDataFile(mod, "creatures",     NPC::LoadPresets);
			
			Data::Mod metadata(mod.filename().string(), "<unknown>", "<unknown>", "<unknown>", -1);
			if (fs::exists(mod / "mod.dat")) {
				Logger::Inst()->output << "[Data] Loading mod metadata.\n";
				
				ModListener *listener = new ModListener(&metadata);
				parser.run((mod / "mod.dat").string().c_str(), listener);
				delete listener;
			}
			
			if (metadata.apiVersion != -1) {
				if (metadata.apiVersion != Script::version) {
					Logger::Inst()->output << "[Data] WARNING: Ignoring mod scripts because of incorrect API version.\n";
				} else {
					Script::LoadScript(metadata.mod, mod.string());
				}
			}
			
			globals::loadedMods.push_back(metadata);
		}
	}
	
	void DoSave(std::string file) {
		Logger::Inst()->output << "[Data] Saving game to " << file << "\n";
		
		Game::Inst()->SaveGame(file);
		Script::Event::GameSaved(file);
		
		Logger::Inst()->output.flush();
	}
}

namespace Data {
	void Init() {
		std::string personalDir, exec;
		fs::path execDir;
		
		// bootstrap to get logging up and running
		FindPersonalDirectory();
		fs::create_directory(globals::personalDir);
		Logger::Create((globals::personalDir / "goblin-camp.log").string());
		
		Logger::Inst()->output << "[Data] Data::Init()\n";
		FindExecutableDirectory();
		
		Logger::Inst()->output <<
			"[Data] Personal directory: " << globals::personalDir << "\n" <<
			"[Data] Saves directory: " << globals::savesDir << "\n" <<
			"[Data] Screenshots directory: " << globals::screensDir << "\n" <<
			"[Data] Mods directory: " << globals::modsDir << "\n" <<
			"[Data] Executable Directory: " << globals::execDir << "\n" <<
			"[Data] Global Data Directory: " << globals::dataDir << "\n" <<
			"[Data] --------\n" <<
			"[Data] Executable: " << globals::exec << "\n" <<
			"[Data] Config: " << globals::config << "\n" <<
			"[Data] Keys: " << globals::keys << "\n" <<
			"[Data] Font: " << globals::font << "\n"
		;
		
		// try to create personal directory structure
		// create_directory doesn't throw if directory already exist
		try {
			fs::create_directory(globals::savesDir);
			fs::create_directory(globals::screensDir);
			fs::create_directory(globals::modsDir);
		} catch (const fs::filesystem_error& e) {
			Logger::Inst()->output << "[Data] filesystem_error while creating directories: " << e.what() << "\n";
			Logger::Inst()->output.flush();
			exit(1);
		}
		
		// now check whether user has config.ini
		//   if not, check whether default one exist (data-dir/config.ini)
		//     if so, copy it to user config.ini
		//     if not, write hardcoded defaults
		if (!fs::exists(globals::config)) {
			Logger::Inst()->output << "[Data] User's config.ini does not exist.\n";
			
			fs::path defaultConfig = globals::dataDir / "config.ini";
			if (!fs::exists(defaultConfig)) {
				defaultConfig = globals::execDir / "config.ini";
			}
			
			if (fs::exists(defaultConfig)) {
				Logger::Inst()->output << "[Data] Copying default config.ini to user directory.\n";
				
				try {
					fs::copy_file(defaultConfig, globals::config);
				} catch (const fs::filesystem_error& e) {
					Logger::Inst()->output << "[Data] filesystem_error while copying config: " << e.what() << "\n";
					Logger::Inst()->output.flush();
					exit(2);
				}
			} else {
				Logger::Inst()->output << "[Data] Creating default config.ini.\n";
				
				try {
					SaveConfig(800, 600, "SDL", false);
				} catch (const std::exception &e) {
					Logger::Inst()->output << "[Data] std::exception while creating config: " << e.what() << "\n";
					Logger::Inst()->output.flush();
					exit(3);
				}
			}
		}
		
		// now check whether user has keys.ini
		//   if not, copy the default one (data-dir/keys.ini)
		if (!fs::exists(globals::keys)) {
			Logger::Inst()->output << "[Data] User's keys.ini does not exist.\n";
			
			if (fs::exists(globals::defaultKeys)) {
				Logger::Inst()->output << "[Data] Copying default keys.ini to user directory.\n";
				
				try {
					fs::copy_file(globals::defaultKeys, globals::keys);
				} catch (const fs::filesystem_error& e) {
					Logger::Inst()->output << "[Data] filesystem_error while copying keys: " << e.what() << "\n";
					Logger::Inst()->output.flush();
					exit(6);
				}
			} else {
				Logger::Inst()->output << "[Data] No keys.ini found in user's directory or in defaults.\n";
				Logger::Inst()->output.flush();
				exit(7);
			}
		}
		
		// check whether custom font exists
		// if not, copy default one
		if (!fs::exists(globals::font)) {
			Logger::Inst()->output << "[Data] User's terminal.png does not exist, copying default.\n";
			
			fs::path defaultFont = globals::dataDir / "terminal.png";
			if (!fs::exists(defaultFont)) {
				defaultFont = globals::execDir / "terminal.png";
			}
			
			try {
				fs::copy_file(defaultFont, globals::font);
			} catch (const fs::filesystem_error& e) {
				Logger::Inst()->output << "[Data] filesystem_error while copying font: " << e.what() << "\n";
				Logger::Inst()->output.flush();
				exit(4);
			}
		}
		
		Logger::Inst()->output << "[Data] Loading config.ini.\n";
		Game::Inst()->LoadConfig(globals::config.string());
		
		Logger::Inst()->output << "[Data] Loading keymaps.\n";
		UI::LoadKeys(globals::defaultKeys.string());
		UI::LoadKeys(globals::keys.string());
		
		Logger::Inst()->output << "[Data] Loading terminal.png.\n";
		TCODConsole::setCustomFont(globals::font.string().c_str());
		
		Logger::Inst()->output << "[Data] Data::Init() finished.\n";
		Logger::Inst()->output.flush();
	}
	
	void Load() {
		Logger::Inst()->output << "[Data] Data::Load()\n";
		
		// load global data files
		LoadGlobalMod();
		
		// and then user mods
		// each mod is a directory with data files, e.g.
		//   + mod1
		//   |- items.dat
		//   |- constructions.dat
		//   |- names.dat
		//   |- wildplants.dat
		//   |- creatures.dat
		//   + mod2
		//   |- ...
		LoadLocalMods();
		
		#ifdef MACOSX
		// workaround
		Logger::Inst()->output << "[Data] Loading terminal.png again.\n";
		TCODConsole::setCustomFont(globals::font.string().c_str());
		#endif
		
		// now resolve containers and products
		Item::ResolveContainers();
		Construction::ResolveProducts();
		
		Logger::Inst()->output << "[Data] Data::Load() finished.\n";
		Logger::Inst()->output.flush();
	}
	
	void GetSavedGames(TCODList<std::string>& list) {
		fs::directory_iterator end;
		for (fs::directory_iterator it(globals::savesDir); it != end; ++it) {
			fs::path save = it->path();
			if (!boost::iequals(save.extension().string(), ".sav")) continue;
			
			save.replace_extension();
			list.push(save.filename().string());
		}
	}
	
	void LoadGame(const std::string& save) {
		std::string file = (globals::savesDir / save).string() + ".sav";
		Logger::Inst()->output << "[Data] Loading game from " << file << "\n";
		
		Game::Inst()->LoadGame(file);
		Script::Event::GameLoaded(file);
		
		Logger::Inst()->output.flush();
	}
	
	void SaveGame(const std::string& save) {
		std::string file = (globals::savesDir / save).string() + ".sav";
		
		if (!fs::exists(file)) {
			DoSave(file);
		} else {
			YesNoDialog::ShowYesNoDialog("Save game exists, overwrite?", boost::bind(DoSave, file), NULL);
		}
	}
	
	void SaveScreenshot() {
		// sadly, libtcod supports autonumbering only when saving to current dir
		fs::directory_iterator end;
		unsigned int largest = 0;
		
		for (fs::directory_iterator it(globals::screensDir); it != end; ++it) {
			fs::path png = it->path();
			if (!boost::iequals(png.extension().string(), ".png")) continue;
			
			png.replace_extension();
			
			std::string file = png.filename().string();
			try {
				// screens are saved as screenXXXXXX.png
				largest = std::max(largest, boost::lexical_cast<unsigned int>(file.substr(6)));
			} catch (const std::exception& e) {
				// not worth terminating the game for
				(void)e; // variable not referenced warning
			}
		}
		
		std::string png = (
			globals::screensDir / ((boost::format("screen%|06|.png") % (largest + 1)).str())
		).string();
		
		Logger::Inst()->output << "[Data] Saving screenshot to " << png << "\n";
		TCODSystem::saveScreenshot(png.c_str());
		
		Logger::Inst()->output.flush();
	}
	
	void SaveConfig(unsigned int resWidth, unsigned int resHeight, const std::string& renderer, bool fullscreen) {
		// It's up to caller to deal with exceptions.
		std::ofstream configStream(globals::config.string().c_str());
		// I hate -th and -ht suffixes.
		configStream <<
			"config {\n\twidth    = " << resWidth <<
			"\n\theight   = " << resHeight << "\n\trenderer = \"" << renderer << "\"" <<
			(fullscreen ? "\n\tfullscreen" : "") <<
		"\n}";
		configStream.close();
	}
	
	void SaveKeys(const std::map<std::string, char>& keymap) {
		typedef std::pair<std::string, char> KeyPair;
		std::ofstream keysStream(globals::keys.string().c_str());
		
		keysStream << "keys {\n";
		BOOST_FOREACH(KeyPair mapping, keymap) {
			keysStream << "\t" << mapping.first << " = '" << mapping.second << "'\n";
		}
		keysStream << "}";
		keysStream.close();
	}
	
	#pragma warning(push)
	// "warning C4715: 'Data::GetPath' : not all control paths return a value"
	#pragma warning(disable : 4715)
	fs::path& GetPath(Path::Path what) {
		switch (what) {
			case Path::Executable:  return globals::exec;
			case Path::GlobalData:  return globals::dataDir;
			case Path::Personal:    return globals::personalDir;
			case Path::Mods:        return globals::modsDir;
			case Path::Saves:       return globals::savesDir;
			case Path::Screenshots: return globals::screensDir;
			case Path::Font:        return globals::font;
			case Path::Config:      return globals::config;
			case Path::Keys:        return globals::keys;
		}
		
		// If control reaches here, then someone added new value to the enum,
		// forgot to add it here, and missed the 'switch not checking
		// every value' warning. So, crash and burn.
		assert(false);
	}
	#pragma warning(pop)
	
	std::list<Mod>& GetLoadedMods() {
		return globals::loadedMods;
	}
}
