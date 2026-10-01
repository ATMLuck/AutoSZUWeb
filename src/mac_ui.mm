#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include "app_ui.h"

namespace
{
    NSString* Utf8String(const std::string& value)
    {
        NSString* string = [NSString stringWithUTF8String:value.c_str()];
        return string ?: @"";
    }
}

@interface AutoSZUWebAlertController : NSObject
- (void)confirm:(id)sender;
- (void)cancel:(id)sender;
- (void)windowWillClose:(NSNotification*)notification;
@end

@implementation AutoSZUWebAlertController

- (void)confirm:(id)sender
{
    (void)sender;
    [NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)cancel:(id)sender
{
    (void)sender;
    [NSApp stopModalWithCode:NSModalResponseCancel];
}

- (void)windowWillClose:(NSNotification*)notification
{
    if (notification.object == NSApp.modalWindow)
        [NSApp stopModalWithCode:NSModalResponseCancel];
}

@end

static AppUI::Button ShowMessageOnMainThread(const std::string& text,
                                              const std::string& title,
                                              bool allowCancel)
{
    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];

    const NSApplicationActivationPolicy previousPolicy = application.activationPolicy;
    [application setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application activateIgnoringOtherApps:YES];

    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleInformational;
    alert.messageText = Utf8String(title);
    alert.informativeText = Utf8String(text);

    AutoSZUWebAlertController* controller =
        [[AutoSZUWebAlertController alloc] init];

    NSButton* confirmButton = [alert addButtonWithTitle:@"确定"];
    confirmButton.target = controller;
    confirmButton.action = @selector(confirm:);

    if (allowCancel)
    {
        NSButton* cancelButton = [alert addButtonWithTitle:@"取消"];
        cancelButton.target = controller;
        cancelButton.action = @selector(cancel:);
    }

    NSWindow* window = alert.window;
    [[NSNotificationCenter defaultCenter]
        addObserver:controller
        selector:@selector(windowWillClose:)
        name:NSWindowWillCloseNotification
        object:window];

    [window center];
    [window makeKeyAndOrderFront:nil];
    [application activateIgnoringOtherApps:YES];

    // Use an explicit modal session. The button actions above terminate this
    // exact loop with stopModalWithCode:, instead of relying on NSAlert's
    // private target/action wiring in an LSUIElement background application.
    const NSModalResponse response = [application runModalForWindow:window];

    [[NSNotificationCenter defaultCenter] removeObserver:controller];
    [window orderOut:nil];
    [controller release];
    [alert release];

    if (previousPolicy != NSApplicationActivationPolicyRegular)
        [application setActivationPolicy:previousPolicy];

    return allowCancel && response != NSModalResponseOK
        ? AppUI::Button::Cancel : AppUI::Button::Ok;
}

AppUI::Button AppUI::ShowMessage(const std::string& text,
                                 const std::string& title,
                                 bool allowCancel)
{
    if ([NSThread isMainThread])
        return ShowMessageOnMainThread(text, title, allowCancel);

    __block AppUI::Button result = AppUI::Button::Ok;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = ShowMessageOnMainThread(text, title, allowCancel);
    });
    return result;
}