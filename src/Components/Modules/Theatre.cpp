#include "STDInclude.hpp"

namespace Components
{
	Theatre::Container Theatre::DemoContainer;

	char Theatre::BaselineSnapshot[131072] = { 0 };
	PBYTE Theatre::BaselineSnapshotMsg = 0;
	int Theatre::BaselineSnapshotMsgLen;
	int Theatre::BaselineSnapshotMsgOff;

	void Theatre::GamestateWriteStub(Game::msg_t* msg, char byte)
	{
		Game::MSG_WriteLong(msg, 0);
		Game::MSG_WriteByte(msg, byte);
	}

	void Theatre::RecordGamestateStub()
	{
		int sequence = (*Game::serverMessageSequence - 1);
		Game::FS_Write(&sequence, 4, *Game::demoFile);
	}

	void __declspec(naked) Theatre::BaselineStoreStub()
	{
		// Store snapshot message
		__asm mov Theatre::BaselineSnapshotMsg, edi

		// Store offset and length
		Theatre::BaselineSnapshotMsgLen = *(int*)(Theatre::BaselineSnapshotMsg + 20);
		Theatre::BaselineSnapshotMsgOff = *(int*)(Theatre::BaselineSnapshotMsg + 28) - 7;

		// Copy to our snapshot buffer
		memcpy(Theatre::BaselineSnapshot, *(DWORD**)(Theatre::BaselineSnapshotMsg + 8), *(DWORD*)(Theatre::BaselineSnapshotMsg + 20));

		__asm
		{
			mov edx, 5ABEF5h
			jmp edx
		}
	}

	void Theatre::WriteBaseline()
	{
		static char bufData[131072];
		static char cmpData[131072];

		Game::msg_t buf;

		Game::MSG_Init(&buf, bufData, 131072);
		Game::MSG_WriteData(&buf, &Theatre::BaselineSnapshot[Theatre::BaselineSnapshotMsgOff], Theatre::BaselineSnapshotMsgLen - Theatre::BaselineSnapshotMsgOff);
		Game::MSG_WriteByte(&buf, 6);

		int compressedSize = Game::MSG_WriteBitsCompress(false, buf.data, cmpData, buf.cursize);
		int fileCompressedSize = compressedSize + 4;

		int byte8 = 8;
		char byte0 = 0;

		Game::FS_Write(&byte0, 1, *Game::demoFile);
		Game::FS_Write(Game::serverMessageSequence, 4, *Game::demoFile);
		Game::FS_Write(&fileCompressedSize, 4, *Game::demoFile);
		Game::FS_Write(&byte8, 4, *Game::demoFile);

		for (int i = 0; i < compressedSize; i += 1024)
		{
			int size = min(compressedSize - i, 1024);

			if (i + size >= sizeof(cmpData))
			{
				Logger::Print("Error: Writing compressed demo baseline exceeded buffer\n");
				break;
			}
			
			Game::FS_Write(&cmpData[i], size, *Game::demoFile);
		}
	}

	void __declspec(naked) Theatre::BaselineToFileStub()
	{
		__asm
		{
			call Theatre::WriteBaseline

			// Restore overwritten operation
			mov ecx, 0A5E9C4h
			mov [ecx], 0

			// Return to original code
			mov ecx, 5A863Ah
			jmp ecx
		}
	}

	void __declspec(naked) Theatre::AdjustTimeDeltaStub()
	{
		__asm
		{
			mov eax, Game::demoPlaying
			mov eax, [eax]
			test al, al
			jz continue

			// delta doesn't drift for demos
			retn

		continue:
			mov eax, 5A1AD0h
			jmp eax
		}
	}

	void __declspec(naked) Theatre::ServerTimedOutStub()
	{
		__asm
		{
			mov eax, Game::demoPlaying
			mov eax, [eax]
			test al, al
			jz continue

			mov eax, 5A8E70h
			jmp eax

		continue:
			mov eax, 0B2BB90h
			mov esi, 5A8E08h
			jmp esi
		}
	}

	void __declspec(naked) Theatre::UISetActiveMenuStub()
	{
		__asm
		{
			mov eax, Game::demoPlaying
			mov eax, [eax]
			test al, al
			jz continue

			mov eax, 4CB49Ch
			jmp eax

		continue:
			mov ecx, [esp + 10h]
			push 10h
			push ecx
			mov eax, 4CB3F6h
			jmp eax
		}
	}

	void Theatre::RecordStub(int channel, char* message, char* file)
	{
		Game::Com_Printf(channel, message, file);

		Theatre::DemoContainer.CurrentInfo.Name = file;
		Theatre::DemoContainer.CurrentInfo.Mapname = Dvar::Var("mapname").Get<const char*>();
		Theatre::DemoContainer.CurrentInfo.Gametype = Dvar::Var("g_gametype").Get<const char*>();
		Theatre::DemoContainer.CurrentInfo.Author = Steam::SteamFriends()->GetPersonaName();
		Theatre::DemoContainer.CurrentInfo.Length = Game::Com_Milliseconds();
		std::time(&Theatre::DemoContainer.CurrentInfo.TimeStamp);
	}

	void Theatre::StopRecordStub(int channel, char* message)
	{
		Game::Com_Printf(channel, message);

		// Store correct length
		Theatre::DemoContainer.CurrentInfo.Length = Game::Com_Milliseconds() - Theatre::DemoContainer.CurrentInfo.Length;

		// Write metadata
		FileSystem::FileWriter meta(Utils::VA("%s.json", Theatre::DemoContainer.CurrentInfo.Name.data()));
		meta.Write(json11::Json(Theatre::DemoContainer.CurrentInfo).dump());
	}

	void Theatre::LoadDemos()
	{
		Theatre::DemoContainer.CurrentSelection = 0;
		Theatre::DemoContainer.Demos.clear();

		auto demos = FileSystem::GetFileList("demos/", "dm_13");

		for (auto demo : demos)
		{
			FileSystem::File meta(Utils::VA("demos/%s.json", demo.data()));

			if (meta.Exists())
			{
				std::string error;
				json11::Json metaObject = json11::Json::parse(meta.GetBuffer(), error);

				if (metaObject.is_object())
				{
					Theatre::Container::DemoInfo info;

					info.Name      = demo.substr(0, demo.find_last_of("."));
					info.Author    = metaObject["author"].string_value();
					info.Gametype  = metaObject["gametype"].string_value();
					info.Mapname   = metaObject["mapname"].string_value();
					info.Length    = (int)metaObject["length"].number_value();
					info.TimeStamp = _atoi64(metaObject["timestamp"].string_value().data());

					Theatre::DemoContainer.Demos.push_back(info);
				}
			}
		}

		// Reverse, latest demo first!
		std::reverse(Theatre::DemoContainer.Demos.begin(), Theatre::DemoContainer.Demos.end());
	}

	void Theatre::DeleteDemo()
	{
		if (Theatre::DemoContainer.CurrentSelection < Theatre::DemoContainer.Demos.size())
		{
			Theatre::Container::DemoInfo info = Theatre::DemoContainer.Demos[Theatre::DemoContainer.CurrentSelection];
	
			Logger::Print("Deleting demo %s...\n", info.Name.data());

			FileSystem::DeleteFile("demos", info.Name + ".dm_13");
			FileSystem::DeleteFile("demos", info.Name + ".dm_13.json");

			// Reload demos
			Theatre::LoadDemos();
		}
	}

	void Theatre::PlayDemo()
	{
		if (Theatre::DemoContainer.CurrentSelection < Theatre::DemoContainer.Demos.size())
		{
			Command::Execute(Utils::VA("demo %s", Theatre::DemoContainer.Demos[Theatre::DemoContainer.CurrentSelection].Name.data()), true);
			Command::Execute("demoback", false);
		}
	}

	unsigned int Theatre::GetDemoCount()
	{
		return Theatre::DemoContainer.Demos.size();
	}

	// Omit column here
	const char* Theatre::GetDemoText(unsigned int item, int column)
	{
		if (item < Theatre::DemoContainer.Demos.size())
		{
			Theatre::Container::DemoInfo info = Theatre::DemoContainer.Demos[item];

			return Utils::VA("%s on %s", Game::UI_LocalizeGameType(info.Gametype.data()), Game::UI_LocalizeMapName(info.Mapname.data()));
		}

		return "";
	}

	void Theatre::SelectDemo(unsigned int index)
	{
		if (index < Theatre::DemoContainer.Demos.size())
		{
			Theatre::DemoContainer.CurrentSelection = index;
			Theatre::Container::DemoInfo info = Theatre::DemoContainer.Demos[index];

			Dvar::Var("ui_demo_mapname").Set(info.Mapname);
			Dvar::Var("ui_demo_mapname_localized").Set(Game::UI_LocalizeMapName(info.Mapname.data()));
			Dvar::Var("ui_demo_gametype").Set(Game::UI_LocalizeGameType(info.Gametype.data()));
			Dvar::Var("ui_demo_length").Set(info.Length); // TODO: Parse as readable string
			Dvar::Var("ui_demo_author").Set(info.Author);
			Dvar::Var("ui_demo_date").Set(std::asctime(std::localtime(&info.TimeStamp)));
		}
	}

	Theatre::Theatre()
	{
		Utils::Hook(0x5A8370, Theatre::GamestateWriteStub, HOOK_CALL).Install()->Quick();
		Utils::Hook(0x5A85D2, Theatre::RecordGamestateStub, HOOK_CALL).Install()->Quick();
		Utils::Hook(0x5ABE36, Theatre::BaselineStoreStub, HOOK_JUMP).Install()->Quick();
		Utils::Hook(0x5A8630, Theatre::BaselineToFileStub, HOOK_JUMP).Install()->Quick();
		Utils::Hook(0x4CB3EF, Theatre::UISetActiveMenuStub, HOOK_JUMP).Install()->Quick();
		Utils::Hook(0x50320E, Theatre::AdjustTimeDeltaStub, HOOK_CALL).Install()->Quick();
		Utils::Hook(0x5A8E03, Theatre::ServerTimedOutStub, HOOK_JUMP).Install()->Quick();

		// Hook commands to enforce metadata generation
		Utils::Hook(0x5A82AE, Theatre::RecordStub, HOOK_CALL).Install()->Quick();
		Utils::Hook(0x5A8156, Theatre::StopRecordStub, HOOK_CALL).Install()->Quick();

		// UIScripts
		UIScript::Add("loadDemos", Theatre::LoadDemos);
		UIScript::Add("launchDemo", Theatre::PlayDemo);
		UIScript::Add("deleteDemo", Theatre::DeleteDemo);

		// Feeder
		UIFeeder::Add(10.0f, Theatre::GetDemoCount, Theatre::GetDemoText, Theatre::SelectDemo);

		// set the configstrings stuff to load the default (empty) string table; this should allow demo recording on all gametypes/maps
		if(!Dedicated::IsDedicated()) Utils::Hook::Set<char*>(0x47440B, "mp/defaultStringTable.csv");
	
		// Change font size
		Utils::Hook::Set<BYTE>(0x5AC854, 2);
		Utils::Hook::Set<BYTE>(0x5AC85A, 2);
	}
}