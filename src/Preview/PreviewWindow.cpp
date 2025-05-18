#include "PreviewWindow.h"
#include "../Workspace.h"
#include <thread>
#include <mutex>
#include <iostream>
#include <kui/KlemmUI.h>
#include <kui/DynamicMarkup.h>
#include <kui/Platform.h>
#include "Toolbar.kui.hpp"
#include "Sidebar.kui.hpp"

using namespace kui;
using namespace kui::markup;

namespace preview
{
	Window* PreviewWindow = nullptr;

	UIBox* ElementBox = nullptr;
	SidebarElement* Sidebar = nullptr;
	ToolbarElement* Toolbar = nullptr;

	DynamicMarkupContext* MarkupContext = nullptr;
	kui::Font* Text = nullptr;
	kui::MarkupStructure::ParseResult CurrentParsed;
	bool ShouldUpdateParsed = false;
	std::mutex ParseMutex;
	std::mutex SidebarMutex;

	std::string OpenedElement;

	Timer ReparseTimer = Timer();

	std::map<std::string, workspace::FileData> Files;
	std::vector<std::string> OpenedFiles;

	void WindowLoop();
	void UpdateParsed();
	void UpdateSidebar();
}

void preview::Init()
{
	if (PreviewWindow)
		return;
	auto WindowThread = std::thread(&WindowLoop);
	WindowThread.detach();
}

void preview::Destroy()
{
	PreviewWindow->Close();
}

void preview::WindowLoop()
{
	app::error::SetErrorCallback([](std::string Error, bool)
		{
			app::MessageBox(Error, "Error", app::MessageBoxType::Error);
		});

	auto DefaultWindowFlags = Window::WindowFlag::Resizable
		| platform::win32::WindowFlag::DarkTitleBar;

	PreviewWindow = new Window("KlemmUI file preview", DefaultWindowFlags | Window::WindowFlag::AlwaysOnTop);
	Text = new Font("C:/Windows/Fonts/seguisb.ttf");

	ElementBox = new UIBackground(true, 0, 0.05f);
	PreviewWindow->Markup.SetDefaultFont(Text);
	PreviewWindow->Markup.SetGetStringFunction([](std::string str) { return str; });
	ElementBox->SetPosition(-1);
	ElementBox->SetMinSize(Vec2f(1.7f, 1.9f));
	ElementBox->SetMaxSize(Vec2f(1.7f, 1.9f));
	ElementBox->SetHorizontalAlign(UIBox::Align::Centered);
	ElementBox->SetVerticalAlign(UIBox::Align::Centered);


	Toolbar = new ToolbarElement();
	Sidebar = new SidebarElement();

	Toolbar->btn->SetImage("res:Pin.png");
	Toolbar->btn->SetText("On top");
	Toolbar->btn->btn->OnClicked = [btn = Toolbar->btn, DefaultWindowFlags]()
		{
			bool AlwaysOnTop = (PreviewWindow->GetWindowFlags() & Window::WindowFlag::AlwaysOnTop)
				== Window::WindowFlag::AlwaysOnTop;

			btn->btn->BorderColor = AlwaysOnTop ? 0.4f : Vec3f(1, 0, 0);

			PreviewWindow->SetWindowFlags(AlwaysOnTop ?
				DefaultWindowFlags
				: DefaultWindowFlags | Window::WindowFlag::AlwaysOnTop);
		};

	while (PreviewWindow->UpdateWindow())
	{
		if (ShouldUpdateParsed && ReparseTimer.Get() > 0.1f)
		{
			UpdateParsed();
			UpdateSidebar();
			ShouldUpdateParsed = false;
			ReparseTimer.Reset();
		}
		ElementBox->SetMinSize(SizeVec(2).GetScreen() - SizeVec(300_px, 36_px).GetScreen());
		ElementBox->SetMaxSize(ElementBox->GetMinSize());
	}

	delete Text;
	delete PreviewWindow;
	PreviewWindow = nullptr;
}

void preview::UpdateParsed()
{
	{
		std::unique_lock g{ ParseMutex };
		MarkupContext = new DynamicMarkupContext();
		MarkupContext->Parsed = &CurrentParsed;
	}

	// Do not throw error messages if an image doesn't exist.
	kui::resource::ErrorOnFail = false;
	PreviewWindow->Markup.ClearGlobals();
	PreviewWindow->UI.SetTexturePath(workspace::CurrentWorkspacePath);

	ElementBox->DeleteChildren();

	auto DynBox = new UIDynMarkupBox(MarkupContext, OpenedElement);
	ElementBox->AddChild(DynBox);
	ElementBox->UpdateElement();
	ElementBox->RedrawElement(true);
}

void preview::UpdateSidebar()
{
	std::unique_lock sg{ SidebarMutex };

	Sidebar->tabBox->DeleteChildren();

	auto OpenedHeader = new SidebarEntry();
	OpenedHeader->SetPadding(4_px);
	OpenedHeader->SetTitle("Opened");
	Sidebar->tabBox->AddChild(OpenedHeader);

	for (auto& file : OpenedFiles)
	{
		for (auto& elem : MarkupContext->Parsed->Elements)
		{
			if (workspace::ConvertFilePath(elem.File) != file)
				continue;

			auto Entry = new SidebarEntry();
			Entry->SetPadding(32_px);
			Entry->SetImage("res:Window.png");
			Entry->SetTitle(elem.FromToken.Text);
			Entry->SetHighlightOpacity(elem.FromToken.Text == OpenedElement ? 1.0f : 0.0f);
			Entry->btn->OnClicked = [Name = elem.FromToken.Text]()
				{
					OpenedElement = Name;
					ShouldUpdateParsed = true;
				};
			Sidebar->tabBox->AddChild(Entry);
		}
	}
}

void preview::LoadParsed(kui::MarkupStructure::ParseResult* From)
{
	std::unique_lock g{ ParseMutex };
	CurrentParsed = *From;
	ShouldUpdateParsed = true;

	{
		std::unique_lock sg{ SidebarMutex };
		Files = workspace::Files;
		OpenedFiles = workspace::OpenedFiles;
	}
}
