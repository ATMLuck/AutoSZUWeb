#import <AppKit/AppKit.h>

#include "app_ui.h"

namespace
{
    NSString* Utf8String(const std::string& value)
    {
        NSString* string = [NSString stringWithUTF8String:value.c_str()];
        return string ?: @"";
    }
}

AppUI::Button AppUI::ShowMessage(const std::string& text, const std::string& title,
                          bool allowCancel)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp activateIgnoringOtherApps:YES];

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = Utf8String(title);
        alert.informativeText = Utf8String(text);
        [alert addButtonWithTitle:@"确定"];
        if (allowCancel)
            [alert addButtonWithTitle:@"取消"];

        const NSModalResponse response = [alert runModal];
        [alert release];
        return allowCancel && response == NSAlertSecondButtonReturn
            ? Button::Cancel : Button::Ok;
    }
}
