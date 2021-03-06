/*
 * Copyright 2004-2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andre Alves Garzia, andre@andregarzia.com
 *		Stephan Assmuß
 *		Axel Dörfler
 *		Hugo Santos
 *		Philippe Saint-Pierre
 *		Vegard Wærp
 * 		Davide Gessa, dak.linux@gmail.com
 */


#include "IPSettingsView.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <GridView.h>
#include <GroupView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>

#include <AutoDeleter.h>

#include "Settings.h"


static const uint32 kMsgApply = 'aply';
static const uint32 kMsgRevert = 'rvrt';
static const uint32 kMsgClose = 'clse';
static const uint32 kMsgField = 'fild';
static const uint32 kMsgInfo = 'info';
static const uint32 kMsgStaticMode = 'stcm';
static const uint32 kMsgDHCPMode = 'dynm';
static const uint32 kMsgDisabledMode = 'disa';
static const uint32	kMsgChange = 'chng';
static const uint32 kMsgNetwork = 'netw';


static void
SetupTextControl(BTextControl *control)
{
	// TODO: Disallow characters, etc.
	// Would be nice to have a real
	// formatted input control
	control->SetModificationMessage(new BMessage(kMsgChange));
}


static bool
MatchPattern(const char* string, const char* pattern)
{
	regex_t compiled;
	bool result = regcomp(&compiled, pattern, REG_NOSUB | REG_EXTENDED) == 0
		&& regexec(&compiled, string, 0, NULL, 0) == 0;
	regfree(&compiled);

	return result;
}


//	#pragma mark -


#undef B_TRANSLATE_CONTEXT
#define B_TRANSLATE_CONTEXT "IPSettingsView"


IPSettingsView::IPSettingsView(BNetworkInterface *interface, Settings *settings)
	:
	BView("IPSettingsView", 0, NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fSocket = socket(AF_INET, SOCK_DGRAM, 0);
	fInterface = interface;
	fSettings = settings;

	// build the GUI
	BGroupLayout* rootLayout = new BGroupLayout(B_VERTICAL);
	SetLayout(rootLayout);

	BGridView* controlsGroup = new BGridView();
	BGridLayout* layout = controlsGroup->GridLayout();

	// insets
	float inset = ceilf(be_plain_font->Size() * 0.7);
	rootLayout->SetInsets(inset, inset, inset, inset);
	rootLayout->SetSpacing(inset);
	layout->SetSpacing(inset, inset);


	BPopUpMenu* modeMenu = new BPopUpMenu("modes");
	modeMenu->AddItem(new BMenuItem(B_TRANSLATE("Static"),
		new BMessage(kMsgStaticMode)));
	modeMenu->AddItem(new BMenuItem(B_TRANSLATE("DHCP"),
		new BMessage(kMsgDHCPMode)));
	modeMenu->AddSeparatorItem();
	modeMenu->AddItem(new BMenuItem(B_TRANSLATE("Disabled"),
		new BMessage(kMsgDisabledMode)));

	BPopUpMenu* networkMenu = new BPopUpMenu("networks");

	fNetworkMenuField = new BMenuField(B_TRANSLATE("Network:"), networkMenu);
	layout->AddItem(fNetworkMenuField->CreateLabelLayoutItem(), 0, 1);
	layout->AddItem(fNetworkMenuField->CreateMenuBarLayoutItem(), 1, 1);

	fTypeMenuField = new BMenuField(B_TRANSLATE("Mode:"), modeMenu);
	layout->AddItem(fTypeMenuField->CreateLabelLayoutItem(), 0, 2);
	layout->AddItem(fTypeMenuField->CreateMenuBarLayoutItem(), 1, 2);

	fIPTextControl = new BTextControl(B_TRANSLATE("IP address:"), "", NULL);
	SetupTextControl(fIPTextControl);

	BLayoutItem* layoutItem = fIPTextControl->CreateTextViewLayoutItem();
	layoutItem->SetExplicitMinSize(BSize(
		fIPTextControl->StringWidth("XXX.XXX.XXX.XXX") + inset,
		B_SIZE_UNSET));

	layout->AddItem(fIPTextControl->CreateLabelLayoutItem(), 0, 3);
	layout->AddItem(layoutItem, 1, 3);

	fNetMaskTextControl = new BTextControl(B_TRANSLATE("Netmask:"), "", NULL);
	SetupTextControl(fNetMaskTextControl);
	layout->AddItem(fNetMaskTextControl->CreateLabelLayoutItem(), 0, 4);
	layout->AddItem(fNetMaskTextControl->CreateTextViewLayoutItem(), 1, 4);

	fGatewayTextControl = new BTextControl(B_TRANSLATE("Gateway:"), "", NULL);
	SetupTextControl(fGatewayTextControl);
	layout->AddItem(fGatewayTextControl->CreateLabelLayoutItem(), 0, 5);
	layout->AddItem(fGatewayTextControl->CreateTextViewLayoutItem(), 1, 5);

	// TODO: Replace the DNS text controls by a BListView with add/remove
	// functionality and so on...
	fPrimaryDNSTextControl = new BTextControl(B_TRANSLATE("DNS #1:"), "",
		NULL);
	SetupTextControl(fPrimaryDNSTextControl);
	layout->AddItem(fPrimaryDNSTextControl->CreateLabelLayoutItem(), 0, 6);
	layout->AddItem(fPrimaryDNSTextControl->CreateTextViewLayoutItem(), 1, 6);

	fSecondaryDNSTextControl = new BTextControl(B_TRANSLATE("DNS #2:"), "",
		NULL);
	SetupTextControl(fSecondaryDNSTextControl);
	layout->AddItem(fSecondaryDNSTextControl->CreateLabelLayoutItem(), 0, 7);
	layout->AddItem(fSecondaryDNSTextControl->CreateTextViewLayoutItem(), 1, 7);

	fDomainTextControl = new BTextControl(B_TRANSLATE("Domain:"), "", NULL);
	SetupTextControl(fDomainTextControl);
	layout->AddItem(fDomainTextControl->CreateLabelLayoutItem(), 0, 8);
	layout->AddItem(fDomainTextControl->CreateTextViewLayoutItem(), 1, 8);

	fErrorMessage = new BStringView("error", "");
	fErrorMessage->SetAlignment(B_ALIGN_LEFT);
	fErrorMessage->SetFont(be_bold_font);
	fErrorMessage->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	layout->AddView(fErrorMessage, 1, 9);

	// button group (TODO: move to window, but take care of
	// enabling/disabling)
	BGroupView* buttonGroup = new BGroupView(B_HORIZONTAL);

	fRevertButton = new BButton(B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));
	fRevertButton->SetEnabled(false);
	buttonGroup->GroupLayout()->AddView(fRevertButton);

	buttonGroup->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());

	fApplyButton = new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgApply));
	fApplyButton->SetEnabled(false);
	buttonGroup->GroupLayout()->AddView(fApplyButton);

	rootLayout->AddView(controlsGroup);
	rootLayout->AddView(buttonGroup);
}


IPSettingsView::~IPSettingsView()
{
	close(fSocket);
}


void
IPSettingsView::AttachedToWindow()
{
	fApplyButton->SetTarget(this);
	fRevertButton->SetTarget(this);
	fIPTextControl->SetTarget(this);
	fNetMaskTextControl->SetTarget(this);
	fGatewayTextControl->SetTarget(this);
	fPrimaryDNSTextControl->SetTarget(this);
	fSecondaryDNSTextControl->SetTarget(this);
	fDomainTextControl->SetTarget(this);
	fDeviceMenuField->Menu()->SetTargetForItems(this);
	fNetworkMenuField->Menu()->SetTargetForItems(this);
	fTypeMenuField->Menu()->SetTargetForItems(this);

	// Display settigs of first adapter on startup, if any
	_ShowConfiguration(fSettings);
}


void
IPSettingsView::DetachedFromWindow()
{
}


void
IPSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStaticMode:
		case kMsgDHCPMode:
		case kMsgDisabledMode:
		case kMsgNetwork:
			_EnableTextControls(message->what == kMsgStaticMode);
			fApplyButton->SetEnabled(true);
			fRevertButton->SetEnabled(true);
			break;
		case kMsgInfo:
		{
		 	const char* name;
			if (message->FindString("interface", &name) != B_OK)
				break;
			if (strcmp(fSettings->Name(), name) == 0) {
				_ShowConfiguration(fSettings);
				break;
			}
			break;
		}
		case kMsgRevert:
			_ShowConfiguration(fSettings);
			fRevertButton->SetEnabled(false);
			break;
		case kMsgApply:
			if (_ValidateControl(fIPTextControl)
				&& _ValidateControl(fNetMaskTextControl)
				&& (strlen(fGatewayTextControl->Text()) == 0
					|| _ValidateControl(fGatewayTextControl))
				&& (strlen(fPrimaryDNSTextControl->Text()) == 0
					|| _ValidateControl(fPrimaryDNSTextControl))
				&& (strlen(fSecondaryDNSTextControl->Text()) == 0
					|| _ValidateControl(fSecondaryDNSTextControl)))
				_SaveConfiguration();
			break;
		case kMsgChange:
			fErrorMessage->SetText("");
			fApplyButton->SetEnabled(true);
			break;
		default:
			BView::MessageReceived(message);
	}
}


void
IPSettingsView::_ShowConfiguration(Settings* settings)
{
	// Clear the inputs.
	fIPTextControl->SetText("");
	fGatewayTextControl->SetText("");
	fNetMaskTextControl->SetText("");
	fPrimaryDNSTextControl->SetText("");
	fSecondaryDNSTextControl->SetText("");
	fDomainTextControl->SetText("");

	fDeviceMenuField->SetEnabled(settings != NULL);
	fTypeMenuField->SetEnabled(settings != NULL);

	bool enableControls = false;
	BMenuItem* item;

	if (settings == NULL || settings->IsDisabled())
		item = fTypeMenuField->Menu()->FindItem(B_TRANSLATE("Disabled"));
	else if (settings->AutoConfigure())
		item = fTypeMenuField->Menu()->FindItem(B_TRANSLATE("DHCP"));
	else {
		item = fTypeMenuField->Menu()->FindItem(B_TRANSLATE("Static"));
		enableControls = true;
	}
	if (item != NULL)
		item->SetMarked(true);

	if (settings == NULL) {
		if (!fNetworkMenuField->IsHidden(fNetworkMenuField))
			fNetworkMenuField->Hide();
		_EnableTextControls(false);
		return;
	}

	// Show/hide networks menu
	BNetworkDevice device(settings->Name());
	if (fNetworkMenuField->IsHidden(fNetworkMenuField) && device.IsWireless()) {
		fNetworkMenuField->Show();
		Window()->InvalidateLayout();
	} else if (!fNetworkMenuField->IsHidden(fNetworkMenuField)
		&& !device.IsWireless()) {
		fNetworkMenuField->Hide();
		Window()->InvalidateLayout();
	}

	item = fDeviceMenuField->Menu()->FindItem(settings->Name());
	if (item != NULL)
		item->SetMarked(true);

	fIPTextControl->SetText(settings->IP());
	fGatewayTextControl->SetText(settings->Gateway());
	fNetMaskTextControl->SetText(settings->Netmask());

	if (settings->NameServers().CountItems() >= 2) {
		fSecondaryDNSTextControl->SetText(
			settings->NameServers().ItemAt(1)->String());
	}

	if (settings->NameServers().CountItems() >= 1) {
		fPrimaryDNSTextControl->SetText(
			settings->NameServers().ItemAt(0)->String());
	}
	fDomainTextControl->SetText(settings->Domain());

	_EnableTextControls(enableControls);
}


void
IPSettingsView::_EnableTextControls(bool enable)
{
	fIPTextControl->SetEnabled(enable);
	fGatewayTextControl->SetEnabled(enable);
	fNetMaskTextControl->SetEnabled(enable);
	fPrimaryDNSTextControl->SetEnabled(enable);
	fSecondaryDNSTextControl->SetEnabled(enable);
	fDomainTextControl->SetEnabled(enable);
}


void
IPSettingsView::_ApplyControlsToConfiguration()
{
	fSettings->SetIP(fIPTextControl->Text());
	fSettings->SetNetmask(fNetMaskTextControl->Text());
	fSettings->SetGateway(fGatewayTextControl->Text());

	if (!fNetworkMenuField->IsHidden(fNetworkMenuField)) {
		if (fNetworkMenuField->Menu()->ItemAt(0)->IsMarked()) {
			fSettings->SetWirelessNetwork(NULL);
		} else {
			BMenuItem* item = fNetworkMenuField->Menu()->FindMarked();
			if (item != NULL)
				fSettings->SetWirelessNetwork(item->Label());
		}
	}

	fSettings->SetAutoConfigure(
		strcmp(fTypeMenuField->Menu()->FindMarked()->Label(),
			B_TRANSLATE("DHCP")) == 0);

	fSettings->SetDisabled(
		strcmp(fTypeMenuField->Menu()->FindMarked()->Label(),
			B_TRANSLATE("Disabled")) == 0);

	fSettings->NameServers().MakeEmpty();
	fSettings->NameServers().AddItem(new BString(
		fPrimaryDNSTextControl->Text()));
	fSettings->NameServers().AddItem(new BString(
		fSecondaryDNSTextControl->Text()));
	fSettings->SetDomain(fDomainTextControl->Text());

	fApplyButton->SetEnabled(false);
	fRevertButton->SetEnabled(true);
}


void
IPSettingsView::_SaveConfiguration()
{
	_ApplyControlsToConfiguration();
	_SaveDNSConfiguration();
	_SaveAdaptersConfiguration();
	if (fSettings->AutoConfigure())
		_TriggerAutoConfig(fSettings->Name());
}


void
IPSettingsView::_SaveDNSConfiguration()
{
	BPath path;
	if (find_directory(B_COMMON_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append("network/resolv.conf");

	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "failed to open %s for writing: %s\n", path.Path(),
			strerror(file.InitCheck()));
		return;
	}

	BString content("# Generated by Network Preflet\n");
	// loop over all adapters

	for (int j = 0; j < fSettings->NameServers().CountItems(); j++) {
		if (fSettings->NameServers().ItemAt(j)->Length() > 0) {
			content << "nameserver\t"
					<< fSettings->NameServers().ItemAt(j)->String()
					<< "\n";
		}
	}
	if (strlen(fSettings->Domain()) > 0) {
		content << "domain\t"
				<< fSettings->Domain()
				<< "\n";
	}
	

	file.Write(content.String(), content.Length());
}


void
IPSettingsView::_SaveAdaptersConfiguration()
{
	BPath path;
	status_t status = _GetPath("interfaces", path);
	if (status < B_OK)
		return;

	FILE* fp = NULL;


	if (fSettings->AutoConfigure() && fSettings->WirelessNetwork() == "")
		return;

	if (fp == NULL) {
		fp = fopen(path.Path(), "w");
		if (fp == NULL) {
			fprintf(stderr, "failed to open file %s to write "
				"configuration: %s\n", path.Path(), strerror(errno));
			return;
		}
	}

	fprintf(fp, "interface %s {\n",
		fSettings->Name());

	if (fSettings->IsDisabled())
		fprintf(fp, "\tdisabled\ttrue\n");
	else if (!fSettings->AutoConfigure()) {
		fprintf(fp, "\taddress {\n");
		fprintf(fp, "\t\tfamily\tinet\n");
		fprintf(fp, "\t\taddress\t%s\n", fSettings->IP());
		fprintf(fp, "\t\tgateway\t%s\n", fSettings->Gateway());
		fprintf(fp, "\t\tmask\t%s\n", fSettings->Netmask());
		fprintf(fp, "\t}\n");
	}
	if (fSettings->WirelessNetwork() != "") {
		fprintf(fp, "\tnetwork\t%s\n",
			fSettings->WirelessNetwork().String());
	}
	fprintf(fp, "}\n\n");
	
	if (fp) {
		printf("%s saved.\n", path.Path());
		fclose(fp);
	} else {
		// all configuration is DHCP, so delete interfaces file.
		remove(path.Path());
	}
}


status_t
IPSettingsView::_TriggerAutoConfig(const char* device)
{
	BNetworkInterface interface(device);
	status_t status = interface.AutoConfigure(AF_INET);

	if (status == B_BAD_PORT_ID) {
		(new BAlert("error", B_TRANSLATE("The net_server needs to run for "
			"the auto configuration!"), B_TRANSLATE("OK")))->Go();
	} else if (status != B_OK) {
		BString errorMessage(B_TRANSLATE("Auto-configuring failed: "));
		errorMessage << strerror(status);
		(new BAlert("error", errorMessage.String(), "OK"))->Go();
	}

	return status;
}


status_t
IPSettingsView::_GetPath(const char* name, BPath& path)
{
	if (find_directory(B_COMMON_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;

	path.Append("network");
	create_directory(path.Path(), 0755);

	if (name != NULL)
		path.Append(name);
	return B_OK;
}


bool
IPSettingsView::_ValidateControl(BTextControl* control)
{
	static const char* pattern = "^(25[0-5]|2[0-4][0-9]|[01][0-9]{2}|[0-9]"
		"{1,2})(\\.(25[0-5]|2[0-4][0-9]|[01][0-9]{2}|[0-9]{1,2})){3}$";

	if (control->IsEnabled() && !MatchPattern(control->Text(), pattern)) {
		control->MakeFocus();
		BString errorMessage;
		errorMessage << control->Label();
		errorMessage.RemoveLast(":");
		errorMessage << " is invalid";
		fErrorMessage->SetText(errorMessage.String());
		beep();
		return false;
	}
	return true;
}
