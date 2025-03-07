#include <StdInc.h>

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"

#include <boost/property_tree/xml_parser.hpp>
#include <sstream>
#include <json.hpp>

#include <CfxSubProcess.h>

class SimpleApp : public CefApp, public CefBrowserProcessHandler {
public:
	SimpleApp();

	// CefApp methods:
	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()
		OVERRIDE {
		return this;
	}

	// CefBrowserProcessHandler methods:
	virtual void OnContextInitialized() OVERRIDE;

	virtual void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) OVERRIDE;

private:
	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(SimpleApp);
};

SimpleApp::SimpleApp()
{

}

void SimpleApp::OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line)
{
	command_line->AppendSwitch("disable-extensions");
	command_line->AppendSwitch("disable-pdf-extension");
	command_line->AppendSwitch("disable-gpu");
	command_line->AppendSwitch("ignore-certificate-errors");
	command_line->AppendSwitch("disable-site-isolation-trials");
	command_line->AppendSwitchWithValue("disable-blink-features", "AutomationControlled");
}

namespace {

	// When using the Views framework this object provides the delegate
	// implementation for the CefWindow that hosts the Views-based browser.
	class SimpleWindowDelegate : public CefWindowDelegate {
	public:
		explicit SimpleWindowDelegate(CefRefPtr<CefBrowserView> browser_view)
			: browser_view_(browser_view) {}

		void OnWindowCreated(CefRefPtr<CefWindow> window) OVERRIDE {
			// Add the browser view and show the window.
			window->AddChildView(browser_view_);
			//window->CenterWindow(CefSize(705, 535));
			window->CenterWindow(CefSize(1280, 720));
			window->Show();

			// Give keyboard focus to the browser view.
			browser_view_->RequestFocus();
		}

		void OnWindowDestroyed(CefRefPtr<CefWindow> window) OVERRIDE {
			browser_view_ = NULL;
		}

		bool CanClose(CefRefPtr<CefWindow> window) OVERRIDE {
			// Allow the window to close if the browser says it's OK.
			CefRefPtr<CefBrowser> browser = browser_view_->GetBrowser();
			if (browser)
				return browser->GetHost()->TryCloseBrowser();
			return true;
		}

	private:
		CefRefPtr<CefBrowserView> browser_view_;

		IMPLEMENT_REFCOUNTING(SimpleWindowDelegate);
		DISALLOW_COPY_AND_ASSIGN(SimpleWindowDelegate);
	};

}  // namespace

class SimpleHandler : public CefClient,
	public CefDisplayHandler,
	public CefLifeSpanHandler,
	public CefLoadHandler,
	public CefRequestHandler,
	public CefResourceRequestHandler{
public:
	explicit SimpleHandler();
	~SimpleHandler();

	// Provide access to the single global instance of this object.
	static SimpleHandler* GetInstance();

	// CefClient methods:
	virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() OVERRIDE {
		return this;
	}
	virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() OVERRIDE {
		return this;
	}
	virtual CefRefPtr<CefRequestHandler> GetRequestHandler() OVERRIDE {
		return this;
	}
	virtual CefRefPtr<CefLoadHandler> GetLoadHandler() OVERRIDE { return this; }

	virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool is_navigation, bool is_download, const CefString& request_initiator, bool& disable_default_handling) OVERRIDE
	{
		return this;
	}

	virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) OVERRIDE;

	// CefDisplayHandler methods:
	virtual void OnTitleChange(CefRefPtr<CefBrowser> browser,
		const CefString& title) OVERRIDE;

	// CefLifeSpanHandler methods:
	virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) OVERRIDE;
	virtual bool DoClose(CefRefPtr<CefBrowser> browser) OVERRIDE;
	virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) OVERRIDE;

	// CefLoadHandler methods:
	virtual void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) OVERRIDE;

	virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) OVERRIDE;

	virtual void OnLoadError(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		ErrorCode errorCode,
		const CefString& errorText,
		const CefString& failedUrl) OVERRIDE;

	virtual ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefRequestCallback> callback) OVERRIDE;

	// Request that all existing browser windows close.
	void CloseAllBrowsers(bool force_close);

	bool IsClosing() const { return is_closing_; }

private:
	// Platform-specific implementation.
	void PlatformTitleChange(CefRefPtr<CefBrowser> browser,
		const CefString& title);

	// List of existing browser windows. Only accessed on the CEF UI thread.
	typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
	BrowserList browser_list_;

	bool is_closing_;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(SimpleHandler);
};

void SimpleApp::OnContextInitialized()
{
	CEF_REQUIRE_UI_THREAD();

	CefRefPtr<SimpleHandler> handler(new SimpleHandler());

	// Specify CEF browser settings here.
	CefBrowserSettings browser_settings;

	std::string url = "https://rgl.rockstargames.com/launcher";

	// Create the BrowserView.
	CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
	handler, url, browser_settings, {}, NULL, NULL);

	// Create the Window. It will show itself after creation.
	CefWindow::CreateTopLevelWindow(new SimpleWindowDelegate(browser_view));
}

namespace {

	SimpleHandler* g_instance = NULL;

}  // namespace

SimpleHandler::SimpleHandler()
	: is_closing_(false) {
	DCHECK(!g_instance);
	g_instance = this;
}

SimpleHandler::~SimpleHandler() {
	g_instance = NULL;
}

// static
SimpleHandler* SimpleHandler::GetInstance() {
	return g_instance;
}

void SimpleHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
	const CefString& title) {
	CEF_REQUIRE_UI_THREAD();

	// Set the title of the window using the Views framework.
	CefRefPtr<CefBrowserView> browser_view =
		CefBrowserView::GetForBrowser(browser);
	if (browser_view) {
		CefRefPtr<CefWindow> window = browser_view->GetWindow();
		if (window)
			window->SetTitle(title);
	}
}

void SimpleHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
	CEF_REQUIRE_UI_THREAD();

	// Add to the list of existing browsers.
	browser_list_.push_back(browser);
}

bool SimpleHandler::DoClose(CefRefPtr<CefBrowser> browser) {
	CEF_REQUIRE_UI_THREAD();

	// Closing the main window requires special handling. See the DoClose()
	// documentation in the CEF header for a detailed description of this
	// process.
	if (browser_list_.size() == 1) {
		// Set a flag to indicate that the window close should be allowed.
		is_closing_ = true;
	}

	// Allow the close. For windowed browsers this will result in the OS close
	// event being sent.
	return false;
}

void SimpleHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
	CEF_REQUIRE_UI_THREAD();

	// Remove from the list of existing browsers.
	BrowserList::iterator bit = browser_list_.begin();
	for (; bit != browser_list_.end(); ++bit) {
		if ((*bit)->IsSame(browser)) {
			browser_list_.erase(bit);
			break;
		}
	}

	if (browser_list_.empty()) {
		// All browser windows have closed. Quit the application message loop.
		CefQuitMessageLoop();
	}
}

void SimpleHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
	CefRefPtr<CefFrame> frame,
	ErrorCode errorCode,
	const CefString& errorText,
	const CefString& failedUrl) {
	CEF_REQUIRE_UI_THREAD();

	// Don't display an error for downloaded files.
	if (errorCode == ERR_ABORTED)
		return;
}

void SimpleHandler::CloseAllBrowsers(bool force_close) {
	if (!CefCurrentlyOn(TID_UI)) {
		// Execute on the UI thread.
		CefPostTask(TID_UI, base::Bind(&SimpleHandler::CloseAllBrowsers, this,
			force_close));
		return;
	}

	if (browser_list_.empty())
		return;

	BrowserList::const_iterator it = browser_list_.begin();
	for (; it != browser_list_.end(); ++it)
		(*it)->GetHost()->CloseBrowser(force_close);

	CefQuitMessageLoop();
}

auto SimpleHandler::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefRequestCallback> callback) -> ReturnValue
{
	CefRequest::HeaderMap hm;
	request->GetHeaderMap(hm);

	for (auto it = hm.begin(); it != hm.end(); )
	{
		if (it->first.ToString().find("sec-") == 0 || it->first.ToString().find("Sec-") == 0)
		{
			it = hm.erase(it);
		}
		else
		{
			it++;
		}
	}

	request->SetHeaderMap(hm);

	return RV_CONTINUE;
}

extern std::string g_rosData;
extern bool g_oldAge;
extern std::string g_rosEmail;

bool SimpleHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
	if (message->GetName() == "invokeNative")
	{
		auto args = message->GetArgumentList();
		auto nativeType = args->GetString(0);
		auto messageData = args->GetString(1);

		if (nativeType == "ui")
		{
			if (messageData == "Close")
			{
				CloseAllBrowsers(false);
			}
			else if (messageData == "Minimize")
			{

			}
			else if (messageData == "Maximize")
			{

			}
		}
		else if (nativeType == "signin")
		{
			trace(__FUNCTION__ ": Processing NUI sign-in.\n");

			auto json = nlohmann::json::parse(messageData.ToString());
			auto response = json["XMLResponse"];

			auto age = json["Age"].get<int>();
			g_rosEmail = json["Email"].get<std::string>();

			// 1900 age
			if (age >= 120)
			{
				g_oldAge = true;
			}

			std::string responseDec;
			UrlDecode(response, responseDec);

			std::istringstream stream(responseDec);

			boost::property_tree::ptree tree;
			boost::property_tree::read_xml(stream, tree);

			auto obj = nlohmann::json::object({
				{ "Ticket", json["ticket"] },
				{ "SessionKey", json["sessionKey"] },
				{ "RockstarId", tree.get<std::string>("Response.RockstarAccount.RockstarId") },
				{ "SessionTicket", tree.get<std::string>("Response.SessionTicket") },
				{ "OrigNickname", json["Nickname"] },
			});

			g_rosData = obj.dump();

			trace(__FUNCTION__ ": Processed NUI sign-in - closing all browsers.\n");

			CloseAllBrowsers(false);
		}
	}

	return true;
}

static std::string g_rgscInitCode = fmt::sprintf(R"(
function RGSC_GET_PROFILE_LIST()
{
	return JSON.stringify({
		Profiles: [],//profileList.profiles,
		NumProfiles: 0//profileList.profiles.length
	});
}

function RGSC_UI_STATE_RESPONSE(arg)
{
	var data = JSON.parse(arg);

	if (!data.Visible)
	{
		$("#scuiPanelInstruction").hide();
		$('#headerWrapper').hide();
	}
}

function RGSC_GET_TITLE_ID()
{
	return JSON.stringify({
		RosTitleName: 'gta5',
		RosEnvironment: 'prod',
		RosTitleVersion: 11,
		RosPlatform: 'pcros',
		Platform: 'viveport',
		IsLauncher: true,
		Language: 'en-US'
	});
}

function RGSC_GET_VERSION_INFO()
{
	return JSON.stringify({
		Version: '2.0.3.7',
		TitleVersion: ''
	});
}

function RGSC_GET_COMMAND_LINE_ARGUMENTS()
{
	return JSON.stringify({
		Arguments: []
	});
}

function RGSC_SIGN_IN(s)
{
	var data = JSON.parse(s);

	if (data.XMLResponse)
	{
		window.invokeNative('signin', s);
	}

	RGSC_JS_FINISH_SIGN_IN(JSON.stringify(data));
}

function RGSC_RAISE_UI_EVENT(a)
{
	const d = JSON.parse(a);

	if (d.EventId === 1) {
		window.invokeNative('ui', d.Data.Action);
	}
}

function RGSC_MODIFY_PROFILE(a)
{
	RGSC_JS_FINISH_MODIFY_PROFILE(a);
}

function RGSC_SIGN_OUT()
{
}

function RGSC_DELETE_PROFILE(a)
{
}

function RGSC_REQUEST_PLAYER_LIST_COUNTS(st)
{
}

function RGSC_REQUEST_PLAYER_LIST(st)
{
}

// yes, this is mistyped as RSGC
function RSGC_LAUNCH_EXTERNAL_WEB_BROWSER(a)
{
	var d = JSON.parse(a);
}

function RGSC_GET_ROS_CREDENTIALS()
{
	return JSON.stringify(rosCredentials);
}

function RGSC_REQUEST_UI_STATE(a)
{
	var data = JSON.parse(a);

	RGSC_UI_STATE_RESPONSE(a);

	data.Online = true;

	RGSC_JS_UI_STATE_RESPONSE(JSON.stringify(data));
}

function RGSC_READY_TO_ACCEPT_COMMANDS()
{
	RGSC_JS_REQUEST_UI_STATE(JSON.stringify({ Visible: true, Online: true, State: "SIGNIN" }));
	return true;
}

RGSC_JS_READY_TO_ACCEPT_COMMANDS();

if (!localStorage.getItem('loadedOnce')) {
	localStorage.setItem('loadedOnce', true);
	setTimeout(() => {
		location.reload();
	}, 500);
}

var css = '.rememberContainer, p[class^="AuthHeader__signUpLink"] { display: none; } .UI__Alert__info .UI__Alert__text { display: none; } .UI__Alert__info .UI__Alert__content:after { content: \'A Rockstar Games Social Club account owning %s is required to play %s.\'; max-width: 600px; display: inline-block; }',
    head = document.head || document.getElementsByTagName('head')[0],
    style = document.createElement('style');

head.appendChild(style);

style.type = 'text/css';
style.appendChild(document.createTextNode(css));
)",
#ifdef GTA_FIVE
"Grand Theft Auto V",
"FiveM"
#else
"Grand Theft Auto IV: Complete Edition",
"LibertyM"
#endif
);

void SimpleHandler::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type)
{
}

void SimpleHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode)
{
	if (frame->GetURL().ToString().find("/launcher") != std::string::npos)
	{
		frame->ExecuteJavaScript(g_rgscInitCode, "https://rgl.rockstargames.com/temp.js", 0);
	}
}

void RunLegitimacyNui()
{
	SetEnvironmentVariable(L"CitizenFX_ToolMode", nullptr);

	// Provide CEF with command-line arguments.
	CefMainArgs main_args(GetModuleHandle(nullptr));

	// Specify CEF global settings here.
	CefSettings settings;
	settings.no_sandbox = true;

	settings.remote_debugging_port = 13173;
	settings.log_severity = LOGSEVERITY_DEFAULT;

	CefString(&settings.log_file).FromWString(MakeRelativeCitPath(L"cef.log"));

	CefString(&settings.browser_subprocess_path).FromWString(MakeCfxSubProcess(L"AuthBrowser", L"chrome"));

	CefString(&settings.locale).FromASCII("en-US");

	std::wstring resPath = MakeRelativeCitPath(L"bin/cef/");

	std::wstring cachePath = MakeRelativeCitPath(L"data\\cache\\authbrowser\\");
	CreateDirectory(cachePath.c_str(), nullptr);

	CefString(&settings.resources_dir_path).FromWString(resPath);
	CefString(&settings.locales_dir_path).FromWString(resPath);
	CefString(&settings.cache_path).FromWString(cachePath);
	CefString(&settings.user_agent).FromWString(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/73.0.3683.75 RockstarGames/2.0.7.5/1.0.33.319/launcher/PC Safari/537.36");

	// SimpleApp implements application-level callbacks for the browser process.
	// It will create the first browser instance in OnContextInitialized() after
	// CEF has initialized.
	CefRefPtr<SimpleApp> app(new SimpleApp);

	trace(__FUNCTION__ ": Initializing CEF.\n");

	// Initialize CEF.
	CefInitialize(main_args, settings, app.get(), nullptr);

	trace(__FUNCTION__ ": Initialized CEF.\n");

	// Run the CEF message loop. This will block until CefQuitMessageLoop() is
	// called.
	CefRunMessageLoop();

	trace(__FUNCTION__ ": Shutting down CEF.\n");

	// Shut down CEF.
	//CefShutdown();

	trace(__FUNCTION__ ": Shut down CEF.\n");
}
