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

@interface AutoSZUWebAlertDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) BOOL didCloseWindow;
@end

@implementation AutoSZUWebAlertDelegate

- (instancetype)init
{
    self = [super init];
    if (self)
        _didCloseWindow = NO;
    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)window
{
    (void)window;
    self.didCloseWindow = YES;
    // Closing the title-bar button does not generate an NSAlert button
    // response, so explicitly abort the modal session before allowing close.
    [NSApp abortModal];
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    self.didCloseWindow = YES;
}

@end

static AppUI::Button ShowMessageOnMainThread(const std::string& text,
                                              const std::string& title,
                                              bool allowCancel)
{
    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];

    const NSApplicationActivationPolicy previousPolicy = application.activationPolicy;
    // A background/accessory app can own an NSAlert, but making it regular for
    // the duration of the modal interaction ensures AppKit gives it keyboard
    // and mouse focus when launched by launchd or Finder.
    [application setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application activateIgnoringOtherApps:YES];

    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleInformational;
    alert.messageText = Utf8String(title);
    alert.informativeText = Utf8String(text);
    [alert addButtonWithTitle:@"确定"];
    if (allowCancel)
        [alert addButtonWithTitle:@"取消"];

    AutoSZUWebAlertDelegate* delegate = [[AutoSZUWebAlertDelegate alloc] init];
    alert.window.delegate = delegate;
    NSButton* closeButton = [alert.window standardWindowButton:NSWindowCloseButton];
    closeButton.enabled = YES;
    [alert.window center];
    [alert.window makeKeyAndOrderFront:nil];
    [application activateIgnoringOtherApps:YES];

    const NSModalResponse response = [alert runModal];
    const bool cancelled = delegate.didCloseWindow ||
        response == NSModalResponseAbort ||
        response == NSModalResponseCancel ||
        response == NSAlertSecondButtonReturn;

    if (alert.window.isVisible)
        [alert.window orderOut:nil];
    alert.window.delegate = nil;
    [delegate release];
    [alert release];

    if (previousPolicy != NSApplicationActivationPolicyRegular)
        [application setActivationPolicy:previousPolicy];

    return allowCancel && cancelled
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