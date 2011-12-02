/*
This file is part of Plugin Manager Plugin for Notepad++

Copyright (C)2009-2010 Dave Brotherstone <davegb@pobox.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "precompiled_headers.h"
#include "PluginList.h"
#include "PluginManager.h"

#include "tinyxml/tinyxml.h"
#include "libinstall/InstallStep.h"
#include "libinstall/DownloadStep.h"
#include "libinstall/InstallStepFactory.h"
#include "libinstall/md5.h"
#include "libinstall/DownloadManager.h"
#include "libinstall/Decompress.h"
#include "libinstall/DirectoryUtil.h"
#include "Utility.h"
#include "WcharMbcsConverter.h"



using namespace std;

typedef BOOL (__cdecl * PFUNCISUNICODE)();


PluginList::PluginList(void)
: _variableHandler(NULL)
{
	_hListsAvailableEvent = CreateEvent(
			NULL,				//   LPSECURITY_ATTRIBUTES
			TRUE,				//   bManualReset
			FALSE,				//   Initial state
			NULL);              //   Event name

}

PluginList::~PluginList(void)
{
	if (_variableHandler)
		delete _variableHandler;

	clearPluginList();
}

void PluginList::init(NppData *nppData)
{
	_nppData = nppData;
	TCHAR configDir[MAX_PATH];
	TCHAR nppDir[MAX_PATH];
	TCHAR pluginDir[MAX_PATH];

	::SendMessage(nppData->_nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(configDir));
	::SendMessage(nppData->_nppHandle, NPPM_GETNPPDIRECTORY, MAX_PATH, reinterpret_cast<LPARAM>(nppDir));
	LPARAM nppVersion = ::SendMessage(nppData->_nppHandle, NPPM_GETNPPVERSION, 0, 0);
	
	int majorVersion = HIWORD(nppVersion);
	int minorVersion = LOWORD(nppVersion);
	TCHAR tmp[10];
	_itot_s(majorVersion, tmp, 10, 10);
	tstring versionString(tmp);
	
	_itot_s(minorVersion, tmp, 10, 10);
	
	
	for(int i = 0; tmp[i]; i++)
	{
		versionString.push_back(_T('.'));
		versionString.push_back(tmp[i]);
	}

	_nppVersion = versionString;
		

	_tcscpy_s(pluginDir, MAX_PATH, nppDir);
	_tcscat_s(pluginDir, MAX_PATH, _T("\\plugins"));
	
	_variableHandler = new VariableHandler();
	_variableHandler->setVariable(_T("NPPDIR"), nppDir);
	_variableHandler->setVariable(_T("PLUGINDIR"), pluginDir);
	_variableHandler->setVariable(_T("CONFIGDIR"), configDir);


	
}


void PluginList::addPluginNames(TiXmlElement* pluginNamesElement)
{
	TiXmlElement *pluginNameNode = pluginNamesElement->FirstChildElement();

	while(pluginNameNode)
	{
		if (pluginNameNode->Attribute(_T("md5")) && pluginNameNode->Attribute(_T("name")))
		{
			_pluginRealNames[pluginNameNode->Attribute(_T("md5"))] = pluginNameNode->Attribute(_T("name"));
		}

		pluginNameNode = (TiXmlElement*)pluginNamesElement->IterateChildren(pluginNameNode);
	}
}


BOOL PluginList::parsePluginFile(CONST TCHAR *filename)
{
	clearPluginList();

	TiXmlDocument doc(filename);
	
	doc.LoadFile();
	if (doc.Error())
	{
#ifdef ALLOW_OVERRIDE_XML_URL
		tstring error = doc.ErrorDesc();
		error += _T(" at row ");
		TCHAR tmp[10];
		tmp[0] = '\0';
		if (!_itot_s(doc.ErrorRow(), tmp, 10, 10))
			error += tmp;

		error += _T(", col ");
		
		if (!_itot_s(doc.ErrorCol(), tmp, 10, 10))
			error += tmp;

		
		::MessageBox(_nppData->_nppHandle, error.c_str(), _T("Error parsing XML File"), 0);
#endif
		return FALSE;
	}

	TiXmlNode *pluginsDoc = doc.FirstChildElement(_T("plugins"));
	if (pluginsDoc)
	{
		TiXmlElement *pluginNode = pluginsDoc->FirstChildElement();
		Plugin *plugin;

		while(pluginNode)
		{
			if (!_tcscmp(pluginNode->Value(), _T("pluginNames")))
			{
				addPluginNames(pluginNode);
			}
			else if (!_tcscmp(pluginNode->Value(), _T("plugin")))
			{
				plugin = new Plugin();

				plugin->setName(pluginNode->Attribute(_T("name")));
				 
				BOOL available = FALSE;

				if (g_isUnicode)
				{
					TiXmlElement *versionUrlElement = pluginNode->FirstChildElement(_T("unicodeVersion"));
					if (versionUrlElement && versionUrlElement->FirstChild())
					{
						plugin->setVersion(PluginVersion(versionUrlElement->FirstChild()->Value()));
						available = TRUE;
					}
				}
				else 
				{
					TiXmlElement *versionUrlElement = pluginNode->FirstChildElement(_T("ansiVersion"));
					if (versionUrlElement && versionUrlElement->FirstChild())
					{
						plugin->setVersion(PluginVersion(versionUrlElement->FirstChild()->Value()));
						available = TRUE;
					}

				}

				/* Notepad++ Version checks */
				TiXmlElement *minVersionElement = pluginNode->FirstChildElement(_T("minNotepadVersion"));
				if (minVersionElement && minVersionElement->FirstChild())
				{
					if (_nppVersion < PluginVersion(minVersionElement->FirstChild()->Value()))
						available = FALSE;
				}


				TiXmlElement *maxVersionElement = pluginNode->FirstChildElement(_T("maxNotepadVersion"));
				if (maxVersionElement && maxVersionElement->FirstChild())
				{
					if (_nppVersion > PluginVersion(maxVersionElement->FirstChild()->Value()))
						available = FALSE;
				}

				/* Plugin attributes - description, author etc */
				TiXmlElement *descriptionUrlElement = pluginNode->FirstChildElement(_T("description"));
				if (descriptionUrlElement && descriptionUrlElement->FirstChild())
					plugin->setDescription(descriptionUrlElement->FirstChild()->Value());

				TiXmlElement *filenameUrlElement = pluginNode->FirstChildElement(_T("filename"));
				if (filenameUrlElement && filenameUrlElement->FirstChild())
					plugin->setFilename(filenameUrlElement->FirstChild()->Value());
				
				TiXmlElement *versionsUrlElement = pluginNode->FirstChildElement(_T("versions"));
				
				if (versionsUrlElement)
				{
					TiXmlElement *versionUrlElement = versionsUrlElement->FirstChildElement(_T("version"));
					while(versionUrlElement)
					{
						plugin->addVersion(versionUrlElement->Attribute(_T("md5")), PluginVersion(versionUrlElement->Attribute(_T("number"))));
						versionUrlElement = (TiXmlElement *)versionsUrlElement->IterateChildren(versionUrlElement);
					}
				}

				TiXmlElement *badVersionsElement = pluginNode->FirstChildElement(_T("badVersions"));
				
				if (badVersionsElement)
				{
					TiXmlElement *versionElement = badVersionsElement->FirstChildElement(_T("version"));
					while(versionElement)
					{
						plugin->addBadVersion(PluginVersion(versionElement->Attribute(_T("number"))), versionElement->Attribute(_T("report")));
						versionElement = (TiXmlElement *)badVersionsElement->IterateChildren(versionElement);
					}
				}



				TiXmlElement *aliasesElement = pluginNode->FirstChildElement(_T("aliases"));
				
				if (aliasesElement)
				{
					TiXmlElement *aliasElement = aliasesElement->FirstChildElement(_T("alias"));
					while(aliasElement)
					{
						_aliases[tstring(aliasElement->Attribute(_T("name")))] = plugin->getName();
						aliasElement = (TiXmlElement *)aliasesElement->IterateChildren(aliasElement);
					}
				}

				/* Installation / Removal */
				TiXmlElement *installElement = pluginNode->FirstChildElement(_T("install"));
				
				addSteps(plugin, installElement, INSTALL);
				
				TiXmlElement *removeElement = pluginNode->FirstChildElement(_T("remove"));
				
				if (NULL != removeElement)
				{
					addSteps(plugin, removeElement, REMOVE);
				}

				

				TiXmlElement *dependencies = pluginNode->FirstChildElement(_T("dependencies"));
				if (dependencies && !dependencies->NoChildren())
				{
					TiXmlElement *dependency = dependencies->FirstChildElement();
					while (dependency)
					{
						// If dependency is another plugin (currently the only supported dependency)
						if (!_tcscmp(dependency->Value(), _T("plugin")))
						{
							const TCHAR* dependencyName = dependency->Attribute(_T("name"));
							if (dependencyName)
								plugin->addDependency(dependencyName);
						}

						dependency = reinterpret_cast<TiXmlElement*>(dependencies->IterateChildren(dependency));
					}
				}

				
				TiXmlElement *authorElement = pluginNode->FirstChildElement(_T("author"));
				if (authorElement && authorElement->FirstChild())
					plugin->setAuthor(authorElement->FirstChild()->Value());

				TiXmlElement *sourceElement = pluginNode->FirstChildElement(_T("sourceUrl"));
				if (sourceElement && sourceElement->FirstChild())
					plugin->setSourceUrl(sourceElement->FirstChild()->Value());


				TiXmlElement *homepageElement = pluginNode->FirstChildElement(_T("homepage"));
				if (homepageElement && homepageElement->FirstChild())
					plugin->setHomepage(homepageElement->FirstChild()->Value());


				TiXmlElement *categoryElement = pluginNode->FirstChildElement(_T("category"));
				if (categoryElement && categoryElement->FirstChild())
					plugin->setCategory(categoryElement->FirstChild()->Value());
				else
					plugin->setCategory(_T("Others"));
				
				TiXmlElement *latestUpdateElement = pluginNode->FirstChildElement(_T("latestUpdate"));
				if (latestUpdateElement && latestUpdateElement->FirstChild())
					plugin->setLatestUpdate(latestUpdateElement->FirstChild()->Value());

				// Check stability, default to "Good"
				TiXmlElement *stabilityElement = pluginNode->FirstChildElement(_T("stability"));
				if (stabilityElement && stabilityElement->FirstChild())
					plugin->setStability(stabilityElement->FirstChild()->Value());
				else
					plugin->setStability(_T("Good"));


				if (available)
					_plugins[plugin->getName()] = plugin;
				
			}

			pluginNode = (TiXmlElement *)pluginsDoc->IterateChildren(pluginNode);
		}
	}
	return TRUE;
}



void PluginList::addSteps(Plugin* plugin, TiXmlElement* installElement, InstallOrRemove ior)
{
	if (!installElement)
		return;

	TiXmlElement *installStepElement = installElement->FirstChildElement();

	InstallStepFactory installStepFactory(_variableHandler);

	while (installStepElement)
	{
		// If it is a unicode tag, then only process the contents if it's a unicode N++
		// or if it's an ansi tag, then only process the contents if it's an ansi N++
		if ((g_isUnicode 
			&& !_tcscmp(installStepElement->Value(), _T("unicode")) 
			&& installStepElement->FirstChild())
			||
			(!g_isUnicode
			&& !_tcscmp(installStepElement->Value(), _T("ansi")) 
			&& installStepElement->FirstChild()))
		{
			addSteps(plugin, installStepElement, ior);
		}
		else 
		{

			std::tr1::shared_ptr<InstallStep> installStep = installStepFactory.create(installStepElement, &g_options.proxyInfo);
			if (installStep.get()) 
			{
				if (INSTALL == ior)
					plugin->addInstallStep(installStep);
				else if (REMOVE == ior)
					plugin->addRemoveStep(installStep);
			}

		}


		
		installStepElement = (TiXmlElement *)installElement->IterateChildren(installStepElement);
	}
}


BOOL PluginList::checkInstalledPlugins(TCHAR *pluginPath)
{
	tstring pluginsFullPathFilter;
	
	pluginsFullPathFilter.append(pluginPath);

	pluginsFullPathFilter += _T("\\plugins\\*.dll");

	WIN32_FIND_DATA foundData;
	HANDLE hFindFile = ::FindFirstFile(pluginsFullPathFilter.c_str(), &foundData);


	if (hFindFile != INVALID_HANDLE_VALUE)
	{
		do 
		{
			
			tstring pluginFilename(pluginPath);
			pluginFilename += _T("\\plugins\\");
			pluginFilename += foundData.cFileName;
			BOOL pluginOK = false;
			tstring pluginName;
			try 
			{
				pluginName = getPluginName(pluginFilename);
				pluginOK = true;
			} 
			catch (...)
			{ 
				pluginOK = false;
			}

			
			
			if (pluginOK)
			{
				PluginContainer::iterator knownPlugin = _plugins.find(pluginName);

				if (knownPlugin == _plugins.end())
				{
					// plugin name is not known, so see if we recognise the hash
					// i.e. is it export plugin which renames itself 
					// (or some other plugin that does the same thing)
					TCHAR hash[(MD5::HASH_LENGTH * 2) + 1];
					if (MD5::hash(pluginFilename.c_str(), hash, (MD5::HASH_LENGTH * 2) + 1))
					{
						tstring hashString(hash);
						map<tstring, tstring>::iterator realNameIter = _pluginRealNames.find(hashString);
						if (realNameIter != _pluginRealNames.end())
						{
							knownPlugin = _plugins.find(realNameIter->second);
						}
					}
				}

				// If still unknown, check the aliases
				if (knownPlugin == _plugins.end())
				{
					map<tstring, tstring>::iterator aliasIter = _aliases.find(pluginName);
					if (aliasIter != _aliases.end())
					{
						knownPlugin = _plugins.find(aliasIter->second);
					}
				}

				// Check if plugin known now
				if (knownPlugin != _plugins.end())
				{
					Plugin* plugin = knownPlugin->second;
					
					// If the plugin is already installed, then make a copy for the list
					if (plugin->isInstalled())
					{
						plugin = new Plugin(*plugin);
					}
					

					plugin->setFilename(foundData.cFileName);

					setInstalledVersion(pluginFilename, plugin);
					
					TCHAR hashBuffer[(MD5LEN * 2) + 1];
					
					if (MD5::hash(pluginFilename.c_str(), hashBuffer, (MD5LEN * 2) + 1))
					{
						tstring hash = hashBuffer;
						plugin->setInstalledVersionFromHash(hash);
					}
				
					if (plugin->getInstalledVersion().getIsBad() 
						|| plugin->getVersion() > plugin->getInstalledVersion())
						_updateablePlugins.push_back(plugin);
					else 
						_installedPlugins.push_back(plugin);
				}
				else
				{
					// Plugin is still not known, so create an empty stub for it
					Plugin* plugin = new Plugin();
					plugin->setName(pluginName);
					plugin->setFilename(foundData.cFileName);
					setInstalledVersion(pluginFilename, plugin);
					
					plugin->setDescription(_T("Unknown plugin - please let us know about this plugin on the forums"));

					_installedPlugins.push_back(plugin);
				}

			}
			
		} while(::FindNextFile(hFindFile, &foundData));

		PluginContainer::iterator iter = _plugins.begin();
		while (iter != _plugins.end())
		{
			if (!iter->second->isInstalled())
			{
				if (g_options.showUnstable || iter->second->getStability() == _T("Good"))
					_availablePlugins.push_back(iter->second);
			}
			++iter;
		}


	}

		
	return TRUE;
}


tstring PluginList::getPluginName(tstring pluginFilename)
{
	HINSTANCE pluginInstance = ::LoadLibrary(pluginFilename.c_str());
	if (pluginInstance)
	{
		PFUNCISUNICODE pFuncIsUnicode = (PFUNCISUNICODE)GetProcAddress(pluginInstance, "isUnicode");
		BOOL isUnicode;
		if (!pFuncIsUnicode || !pFuncIsUnicode())
			isUnicode = FALSE;
		else
			isUnicode = TRUE;

		PFUNCGETNAME pFuncGetName = (PFUNCGETNAME)GetProcAddress(pluginInstance, "getName");
		if (pFuncGetName)
		{
			CONST TCHAR* pluginName = pFuncGetName();
			if (pluginName)
			{
				tstring tpluginName = pluginName;
				tstring::size_type ampPosition = tpluginName.find(_T("&"));
				while(ampPosition != tstring::npos)
				{
					tpluginName.replace(ampPosition, 1, _T(""));
					ampPosition = tpluginName.find(_T("&"));
				}

				::FreeLibrary(pluginInstance);
				
				return tpluginName;
			}
			else
			{
				::FreeLibrary(pluginInstance);
				return _T("");
			}
		
		}
		else
		{
			::FreeLibrary(pluginInstance);
			throw tstring(_T("Plugin name call not defined"));
		}
	}
	else
	{
		throw tstring(_T("Load library failed"));
	}
	
}

BOOL PluginList::setInstalledVersion(tstring pluginFilename, Plugin* plugin)
{
	DWORD handle;
	DWORD bufferSize = ::GetFileVersionInfoSize(pluginFilename.c_str(), &handle);
	
	if (bufferSize <= 0) 
		return FALSE;

	unsigned char* buffer = new unsigned char[bufferSize];
	::GetFileVersionInfo(pluginFilename.c_str(), handle, bufferSize, buffer);
	
	struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} *lpTranslate;

	UINT cbTranslate;

	VerQueryValue(buffer, 
              _T("\\VarFileInfo\\Translation"),
              (LPVOID*)&lpTranslate,
              &cbTranslate);

	if (cbTranslate)
	{

		HRESULT hr;
		TCHAR subBlock[50];
		hr = StringCchPrintf(subBlock, 50,
				_T("\\StringFileInfo\\%04x%04x\\FileVersion"),
				lpTranslate[0].wLanguage,
				lpTranslate[0].wCodePage);

		if (FAILED(hr))
		{
			return FALSE;
		}
		else
		{
			TCHAR *fileVersion;
			UINT fileVersionLength;
			if(::VerQueryValue(buffer, subBlock, reinterpret_cast<LPVOID *>(&fileVersion), &fileVersionLength))
			{	
				if (fileVersion)
					plugin->setInstalledVersion(PluginVersion(fileVersion));
			}
		}
	}
	else
	{
		return FALSE;
	}

	return TRUE;


}

void PluginList::waitForListsAvailable()
{
	::WaitForSingleObject(_hListsAvailableEvent, INFINITE);
}

BOOL PluginList::listsAvailable()
{
	DWORD result = ::WaitForSingleObject(_hListsAvailableEvent, 0);
	if (result == WAIT_OBJECT_0)
		return TRUE;
	else
		return FALSE;
}


PluginListContainer& PluginList::getInstalledPlugins()
{
	return _installedPlugins;
}

PluginListContainer& PluginList::getAvailablePlugins()
{
	return _availablePlugins;
}

PluginListContainer& PluginList::getUpdateablePlugins()
{
	return _updateablePlugins;
}

VariableHandler* PluginList::getVariableHandler()
{
	return _variableHandler;
}

Plugin* PluginList::getPlugin(tstring name)
{
	return _plugins[name];
}

BOOL PluginList::isInstallOrUpgrade(const tstring& name)
{
	Plugin* plugin = _plugins[name];
	if (plugin->isInstalled() && plugin->getVersion() <= plugin->getInstalledVersion())
		return FALSE;
	else
		return TRUE;
}




shared_ptr< list<tstring> > PluginList::calculateDependencies(std::tr1::shared_ptr< list<Plugin*> > selectedPlugins)
{
	set<tstring> toBeInstalled;
	std::tr1::shared_ptr< list<tstring> > installDueToDepends(new list<tstring>);


	// First add all selected plugins to a name map
	list<Plugin*>::iterator pluginIter = selectedPlugins->begin();
	while (pluginIter != selectedPlugins->end())
	{
		toBeInstalled.insert((*pluginIter)->getName());
		++pluginIter;
	}

	// Now check all dependencies are in the name map
	pluginIter = selectedPlugins->begin();
	while(pluginIter != selectedPlugins->end())
	{
		if ((*pluginIter)->hasDependencies())
		{
			
			list<tstring> dependencies = (*pluginIter)->getDependencies();
			list<tstring>::iterator depIter = dependencies.begin();
			while(depIter != dependencies.end())
			{
				if (toBeInstalled.count(*depIter) == 0)
				{
					// if not already selected to be installed, then add it to the list
					if (isInstallOrUpgrade(*depIter))
					{
						Plugin* dependsPlugin = getPlugin(*depIter);
						toBeInstalled.insert(*depIter);

						selectedPlugins->push_back(dependsPlugin);
						// Add the name to the list to show the message
						installDueToDepends->push_back(*depIter); 
					}
				}

				++depIter;
			}
		}
		++pluginIter;

	}

	return installDueToDepends;
}

void PluginList::downloadList()
{
		// Work out the path of the Plugins.xml destination (in config dir)
	TCHAR pluginConfig[MAX_PATH];
	::SendMessage(_nppData->_nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH - 26, reinterpret_cast<LPARAM>(pluginConfig));
	
	if (!::PathFileExists(pluginConfig))
	{
		DirectoryUtil::createDirectories(pluginConfig);
	}

	tstring pluginsListFilename(pluginConfig);
	tstring pluginsListZipFilename(pluginConfig);

	pluginsListFilename.append(_T("\\PluginManagerPlugins.xml"));
	pluginsListZipFilename.append(_T("\\PluginManagerPlugins.zip"));
	

	// Download the plugins.xml from the repository
	DownloadManager downloadManager;
	tstring contentType;
	TCHAR hashBuffer[(MD5LEN * 2) + 1];
	MD5::hash(pluginsListFilename.c_str(), hashBuffer, (MD5LEN * 2) + 1);
	string serverMD5;

#ifdef ALLOW_OVERRIDE_XML_URL
	BOOL downloadResult = downloadManager.getUrl(g_options.downloadMD5Url.c_str(), serverMD5, &g_options.proxyInfo, &g_options.moduleInfo);
#else
	BOOL downloadResult = downloadManager.getUrl(PLUGINS_MD5_URL, serverMD5, &g_options.proxyInfo, &g_options.moduleInfo);
#endif

	std::tr1::shared_ptr<char> cHashBuffer = WcharMbcsConverter::tchar2char(hashBuffer);

	
	if (downloadResult && serverMD5 != cHashBuffer.get())
	{
		// If the build is allowing to override the download URL, then use the one from options
		// Also, don't unzip it - assume if it's overridden, it's a test version and hence easier to treat it 
		// as a plain xml file
#ifdef ALLOW_OVERRIDE_XML_URL
		downloadManager.getUrl(g_options.downloadUrl.c_str(), pluginsListFilename, contentType, &g_options.proxyInfo, &g_options.moduleInfo);
#else
		downloadManager.getUrl(PLUGINS_URL, pluginsListZipFilename, contentType, &g_options.proxyInfo, &g_options.moduleInfo);

		// Unzip the plugins.zip to PluginManagerPlugins.xml
		tstring unzipPath(pluginConfig);
		unzipPath.append(_T("\\"));
		Decompress::unzip(pluginsListZipFilename, unzipPath);
#endif
	}



	// Parse it
	parsePluginFile(pluginsListFilename.c_str());
	
	// Check for what is installed
	TCHAR nppDirectory[MAX_PATH];
	::SendMessage(_nppData->_nppHandle, NPPM_GETNPPDIRECTORY, MAX_PATH, reinterpret_cast<LPARAM>(nppDirectory));

	checkInstalledPlugins(nppDirectory);

	::SetEvent(_hListsAvailableEvent);
	
}

void PluginList::reparseFile(const tstring& pluginsListFilename)
{
	// Parse it
	parsePluginFile(pluginsListFilename.c_str());
	
	// Check for what is installed
	TCHAR nppDirectory[MAX_PATH];
	::SendMessage(_nppData->_nppHandle, NPPM_GETNPPDIRECTORY, MAX_PATH, reinterpret_cast<LPARAM>(nppDirectory));

	checkInstalledPlugins(nppDirectory);
}


TiXmlDocument* PluginList::getGpupDocument(const TCHAR* filename)
{
	TiXmlDocument* forGpupDoc = new TiXmlDocument();
	TiXmlElement* installElement = NULL;

	// Load gpup doc if it already exists
	if (::PathFileExists(filename))
	{
		forGpupDoc->LoadFile(filename);
		installElement = forGpupDoc->FirstChildElement(_T("install"));
	}

	// If install element not there, then create it
	if (!installElement)
	{
		installElement = new TiXmlElement(_T("install"));
		forGpupDoc->LinkEndChild(installElement);
	}

	return forGpupDoc;
}

void PluginList::installPlugins(HWND hMessageBoxParent, ProgressDialog* progressDialog, PluginListView* pluginListView, BOOL isUpgrade)
{
	

	// Check if Plugin Manager has an update, and the list is not only updating this plugin
	// If not, then the user *really* ought to do that first, as the XML may have changed
	// or, the URL for the XML may have changed

	g_options.moduleInfo.setHParent(hMessageBoxParent);
	std::tr1::shared_ptr< list<Plugin*> > selectedPlugins = pluginListView->getSelectedPlugins();

	if (selectedPlugins.get() == NULL)
	{
		progressDialog->close();
		return;
	}

	tstring pluginManagerName = _T("Plugin Manager");
	Plugin* pluginManagerPlugin = getPlugin(pluginManagerName);

	if (pluginManagerPlugin && pluginManagerPlugin->getInstalledVersion() < pluginManagerPlugin->getVersion())
	{
		// So there IS an upgrade to plugin manager, so check that it's the only thing in the list
		if (selectedPlugins->size() != 1 || (*(selectedPlugins->begin()))->getName() != _T("Plugin Manager"))
		{
			int updatePM = ::MessageBox(hMessageBoxParent, _T("An update is available to Plugin Manager.  ")
				                            _T("It is *strongly* advised that the Plugin Manager is updated ")
											_T("before any other plugin is installed or updated.  Would you ")
											_T("like to update the Plugin Manager now?"), _T("Plugin Manager"), 
											MB_ICONWARNING | MB_YESNOCANCEL);

			switch(updatePM)
			{
				case IDYES:
					selectedPlugins->clear();
					selectedPlugins->push_back(pluginManagerPlugin);
					isUpgrade = TRUE;
					break;

				case IDNO:
					break;

				case IDCANCEL:
					progressDialog->close();
					return;

			}
		}
	}


	tstring configDir = _variableHandler->getVariable(_T("CONFIGDIR"));
	
	tstring basePath(configDir);

	basePath.append(_T("\\plugin_install_temp"));

	// Create the temp directory if it doesn't exist already
	::CreateDirectory(basePath.c_str(), NULL);
	basePath.append(_T("\\plugin"));
	
	tstring gpupFile(configDir);
	gpupFile.append(_T("\\PluginManagerGpup.xml"));

	TiXmlDocument* forGpupDoc = getGpupDocument(gpupFile.c_str()); 
	TiXmlElement* installElement = forGpupDoc->FirstChildElement(_T("install"));

	
	
	

	std::tr1::shared_ptr< list<tstring> > installDueToDepends = calculateDependencies(selectedPlugins);
		
	if (!installDueToDepends->empty())
	{
		tstring dependsMessage = _T("The following plugin");
		if (installDueToDepends->size() > 1)
			dependsMessage.append(_T("s"));

		dependsMessage.append(_T(" need to be installed to support your selection.\r\n\r\n"));
		for(list<tstring>::iterator msgIter = installDueToDepends->begin(); msgIter != installDueToDepends->end(); msgIter++)
		{
			dependsMessage.append(*msgIter);
			dependsMessage.append(_T("\r\n"));
		}

		dependsMessage.append(_T("\r\nThey will be installed automatically."));


		::MessageBox(hMessageBoxParent, dependsMessage.c_str(), _T("Plugin Manager"), MB_OK | MB_ICONINFORMATION);

	}

	

	size_t installSteps = 0;
	list<Plugin*>::iterator pluginIter = selectedPlugins->begin();
	while(pluginIter != selectedPlugins->end())
	{
		installSteps += (*pluginIter)->getInstallStepCount();
		++pluginIter;
	}

	progressDialog->setStepCount(installSteps);

	pluginIter = selectedPlugins->begin();

	tstring pluginDir = _variableHandler->getVariable(_T("PLUGINDIR"));

	tstring pluginTemp;
	int pluginCount = 1;
	
	BOOL needRestart = FALSE;
	BOOL somethingInstalled = FALSE;

	TCHAR pluginCountChar[10];

	while(pluginIter != selectedPlugins->end())
	{
		BOOL directoryCreated = FALSE;
		do 
		{
			pluginTemp = basePath;
			_itot_s(pluginCount, pluginCountChar, 10, 10);
			pluginTemp.append(pluginCountChar);
			directoryCreated = ::CreateDirectory(pluginTemp.c_str(), NULL);
			++pluginCount;
		} while(!directoryCreated && pluginCount < 500);

		// Check if there's been no luck creating the temp directory
		// Assume there's either no space or no permission
		if (pluginCount >= 500)
		{
			::MessageBox(hMessageBoxParent, _T("Error creating temporary directory for plugin download.  Ensure you have permission to your plugin config directory."), _T("Plugin Manager"), MB_ICONERROR);
			progressDialog->close();
			// nothing to clean up, any created temp directories will be cleaned up if necessary on next launch, once any successful installs have happened
			return;
		}


		pluginTemp.append(_T("\\"));
		


		if (isUpgrade)
		{
			/* Remove the existing file if is an upgrade
			 * This will be done in gpup, but the copy will come afterwards, also in gpup
			 * So, if the filename is the same as the existing plugin, gpup will delete the old
			 * file, then copy in the new.
			 * If the filename is different, the new one will be copied in now, then
			 * the old file will be deleted in gpup.  This is why it is important that
			 * replace="false" (default) on the actual plugin file copy step
			 */

			TiXmlElement* removeElement = new TiXmlElement(_T("delete"));
			
			tstring fullFilename(pluginDir);
			fullFilename.push_back(_T('\\'));
			fullFilename.append((*pluginIter)->getFilename());

			removeElement->SetAttribute(_T("file"), fullFilename.c_str());
			installElement->LinkEndChild(removeElement);
		}

		InstallStatus status = (*pluginIter)->install(pluginTemp, installElement, 
			boost::bind(&ProgressDialog::setCurrentStatus, progressDialog, _1),
			boost::bind(&ProgressDialog::setStepProgress, progressDialog, _1),
			boost::bind(&ProgressDialog::stepComplete, progressDialog),
			&g_options.moduleInfo, _variableHandler);

		switch(status)
		{
			case INSTALL_SUCCESS:
				Utility::removeDirectory(pluginTemp.c_str());
				somethingInstalled = TRUE;
				break;

			case INSTALL_NEEDRESTART:
				needRestart = TRUE;
				somethingInstalled = TRUE;
				break;

			case INSTALL_FAIL:
			{
				tstring message (_T("Installation of "));
				message.append((*pluginIter)->getName());
				message.append(_T(" failed."));

				::MessageBox(hMessageBoxParent, message.c_str(), _T("Installation Error"), MB_OK | MB_ICONERROR);
				Utility::removeDirectory(pluginTemp.c_str());
				break;
			}

		}

		++pluginIter;
	}
	
	
	progressDialog->close(); 


	if (needRestart)
	{
		

		forGpupDoc->SaveFile(gpupFile.c_str());
		delete forGpupDoc;

		int restartNow = ::MessageBox(hMessageBoxParent, _T("Some installation steps still need to be completed.  Notepad++ needs to be restarted in order to complete these steps.  If you restart later, you will be prompted again.  Would you like to restart now?"), _T("Plugin Manager"), MB_YESNO | MB_ICONINFORMATION);
		if (restartNow == IDYES)
		{
			
			tstring gpupArguments(_T("-a \""));
			gpupArguments.append(gpupFile);
			gpupArguments.append(_T("\""));

			Utility::startGpup(hMessageBoxParent, _variableHandler->getVariable(_T("NPPDIR")).c_str(), gpupArguments.c_str(), TRUE);
		}
	}
	else if (somethingInstalled)
	{
		delete forGpupDoc;

		int restartNow = ::MessageBox(hMessageBoxParent, _T("Notepad++ needs to be restarted for changes to take effect.  Would you like to do this now?"), _T("Plugin Manager"), MB_YESNO | MB_ICONINFORMATION);
		if (restartNow == IDYES)
		{
			Utility::startGpup(hMessageBoxParent, _variableHandler->getVariable(_T("NPPDIR")).c_str(), _T(""), FALSE);
		}
	}
	else
	{
		delete forGpupDoc;
	}
	
}

void PluginList::removePlugins(HWND hMessageBoxParent, ProgressDialog* progressDialog, PluginListView* pluginListView)
{
	g_options.moduleInfo.setHParent(hMessageBoxParent);

	tstring configDir = _variableHandler->getVariable(_T("CONFIGDIR"));
	
	tstring basePath(configDir);

	tstring gpupFile(configDir);
	gpupFile.append(_T("\\PluginManagerGpup.xml"));
	
	TiXmlDocument* forGpupDoc = getGpupDocument(gpupFile.c_str());
	TiXmlElement*  installElement = forGpupDoc->FirstChildElement(_T("install"));

	std::tr1::shared_ptr< list<Plugin*> > selectedPlugins = pluginListView->getSelectedPlugins();
		

	if (selectedPlugins.get() == NULL)
	{
		delete forGpupDoc;
		progressDialog->close();
		return;
	}
	
	size_t removeSteps = 0;
	list<Plugin*>::iterator pluginIter = selectedPlugins->begin();
	
	while(pluginIter != selectedPlugins->end())
	{
		removeSteps += (*pluginIter)->getRemoveStepCount();
		++pluginIter;
	}

	progressDialog->setStepCount(removeSteps);

	tstring pluginDir = _variableHandler->getVariable(_T("PLUGINDIR"));
	tstring removeBasePath;

	pluginIter = selectedPlugins->begin();
	
	while(pluginIter != selectedPlugins->end())
	{
		
		(*pluginIter)->remove(removeBasePath, installElement, 
					boost::bind(&ProgressDialog::setCurrentStatus, progressDialog, _1),
					boost::bind(&ProgressDialog::setStepProgress, progressDialog, _1),
					boost::bind(&ProgressDialog::stepComplete, progressDialog),
					&g_options.moduleInfo, _variableHandler);
		++pluginIter;
	}


	

	forGpupDoc->SaveFile(gpupFile.c_str());
	delete forGpupDoc;

	progressDialog->close();

	int restartNow = ::MessageBox(hMessageBoxParent, _T("Notepad++ needs to be restarted in order to complete the removal.  If you restart later, you will be prompted again.  Would you like to restart now?"), _T("Plugin Manager"), MB_YESNO | MB_ICONINFORMATION);
	if (restartNow == IDYES)
	{
		
		tstring gpupArguments(_T("-a \""));
		gpupArguments.append(gpupFile);
		gpupArguments.append(_T("\""));

		Utility::startGpup(hMessageBoxParent, _variableHandler->getVariable(_T("NPPDIR")).c_str(), gpupArguments.c_str(), TRUE);
	}
	else
	{
		pluginListView->removeSelected();
	}
	
	
}




struct InstallParam
{
	PluginList*          pluginList;
	PluginListView*      pluginListView;
	ProgressDialog*		 progressDialog;
	HWND                 hMessageBoxParent;
	BOOL                 isUpdate;
};

void PluginList::startInstall(HWND hMessageBoxParent, 
							  ProgressDialog* progressDialog, 
							  PluginListView *pluginListView, 
							  BOOL isUpdate)
{
	InstallParam *ip = new InstallParam;

	ip->pluginListView    = pluginListView;
	ip->progressDialog    = progressDialog;
	ip->pluginList        = this;
	ip->isUpdate          = isUpdate;
	ip->hMessageBoxParent = hMessageBoxParent;

	::CreateThread(0, 0, (LPTHREAD_START_ROUTINE)PluginList::installThreadProc, 
		(LPVOID)ip, 0, 0);
}


UINT PluginList::installThreadProc(LPVOID param)
{
	InstallParam *ip = reinterpret_cast<InstallParam*>(param);
	
	ip->pluginList->installPlugins(ip->hMessageBoxParent, 
								   ip->progressDialog, 
								   ip->pluginListView, 
								   ip->isUpdate);

	// clean up the parameter
	delete ip;

	return 0;
}




void PluginList::startRemove(HWND hMessageBoxParent, 
							  ProgressDialog* progressDialog, 
							  PluginListView *pluginListView)
{
	InstallParam *ip = new InstallParam;

	ip->pluginListView    = pluginListView;
	ip->progressDialog    = progressDialog;
	ip->pluginList        = this;
	ip->hMessageBoxParent = hMessageBoxParent;

	::CreateThread(0, 0, (LPTHREAD_START_ROUTINE)PluginList::removeThreadProc, 
		(LPVOID)ip, 0, 0);
}

UINT PluginList::removeThreadProc(LPVOID param)
{
	InstallParam *ip = reinterpret_cast<InstallParam*>(param);
	
	ip->pluginList->removePlugins(ip->hMessageBoxParent, 
								   ip->progressDialog, 
								   ip->pluginListView);

	// clean up the parameter
	delete ip;

	return 0;
}

void PluginList::clearPluginList()
{
	PluginContainer::iterator iter = _plugins.begin();
	while (iter != _plugins.end())
	{
		delete iter->second;
		iter++;
	}

	_plugins.clear();
	_installedPlugins.clear();
	_updateablePlugins.clear();
	_availablePlugins.clear();
	_pluginRealNames.clear();
}


