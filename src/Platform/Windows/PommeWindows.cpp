#undef NOUSER

#include "PommeDebug.h"
#include "Platform/Windows/PommeWindows.h"

#include <shlobj.h>

std::filesystem::path Pomme::Platform::Windows::GetPreferencesFolder()
{
	PWSTR wpath = nullptr;
	HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &wpath);

	std::filesystem::path path = "";

	if (result == S_OK)
	{
		path = std::filesystem::path(wpath);
	}

	CoTaskMemFree(static_cast<void*>(wpath));
	return path;
}

void Pomme::Platform::Windows::SysBeep()
{
	MessageBeep(0);
}
