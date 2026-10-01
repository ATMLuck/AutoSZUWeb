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

    NSTextField* MakeLabel(NSString* text, NSFont* font, NSTextAlignment alignment)
    {
        NSTextField* label = [NSTextField labelWithString:text];
        label.translatesAutoresizingMaskIntoConstraints = NO;
        label.font = font;
        label.alignment = alignment;
        label.textColor = [NSColor labelColor];
        label.lineBreakMode = NSLineBreakByWordWrapping;
        label.maximumNumberOfLines = 0;
        return label;
    }
}

@interface AutoSZUWebPanelController : NSObject <NSWindowDelegate>
@property(nonatomic, assign) BOOL didCloseWindow;
- (void)confirm:(id)sender;
- (void)cancel:(id)sender;
@end

@implementation AutoSZUWebPanelController

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

- (BOOL)windowShouldClose:(NSWindow*)window
{
    (void)window;
    self.didCloseWindow = YES;
    [NSApp stopModalWithCode:NSModalResponseCancel];
    return YES;
}

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    self.didCloseWindow = YES;
}

@end

static NSPanel* CreateMessagePanel(const std::string& text,
                                   const std::string& title,
                                   bool allowCancel,
                                   AutoSZUWebPanelController* controller)
{
    const NSRect frame = NSMakeRect(0, 0, 520, allowCancel ? 235 : 215);
    NSPanel* panel = [[NSPanel alloc]
        initWithContentRect:frame
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
        backing:NSBackingStoreBuffered
        defer:NO];
    panel.identifier = @"com.autoszuweb.modal-panel";
    panel.title = Utf8String(title);
    panel.titleVisibility = NSWindowTitleHidden;
    panel.titlebarAppearsTransparent = YES;
    panel.movableByWindowBackground = YES;
    panel.level = NSModalPanelWindowLevel;
    panel.hasShadow = YES;
    panel.releasedWhenClosed = NO;
    panel.becomesKeyOnlyIfNeeded = NO;
    panel.hidesOnDeactivate = NO;
    panel.delegate = controller;

    NSView* content = panel.contentView;
    NSTextField* titleLabel = MakeLabel(Utf8String(title),
        [NSFont boldSystemFontOfSize:24.0], NSTextAlignmentCenter);
    NSTextField* messageLabel = MakeLabel(Utf8String(text),
        [NSFont systemFontOfSize:18.0], NSTextAlignmentCenter);

    NSButton* confirmButton = [NSButton buttonWithTitle:@"确定"
        target:controller action:@selector(confirm:)];
    confirmButton.translatesAutoresizingMaskIntoConstraints = NO;
    confirmButton.bezelStyle = NSBezelStyleRounded;
    confirmButton.keyEquivalent = @"\r";
    confirmButton.font = [NSFont systemFontOfSize:18.0];

    NSStackView* buttons = [NSStackView stackViewWithViews:@[confirmButton]];
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.alignment = NSLayoutAttributeCenterY;
    buttons.distribution = NSStackViewDistributionFillEqually;
    buttons.spacing = 12.0;

    if (allowCancel)
    {
        NSButton* cancelButton = [NSButton buttonWithTitle:@"取消"
            target:controller action:@selector(cancel:)];
        cancelButton.translatesAutoresizingMaskIntoConstraints = NO;
        cancelButton.bezelStyle = NSBezelStyleRounded;
        cancelButton.font = [NSFont systemFontOfSize:18.0];
        [buttons addArrangedSubview:cancelButton];
    }

    NSStackView* stack = [NSStackView stackViewWithViews:@[
        titleLabel, messageLabel, buttons
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeWidth;
    stack.distribution = NSStackViewDistributionFill;
    stack.spacing = 14.0;
    stack.edgeInsets = NSEdgeInsetsMake(26.0, 28.0, 24.0, 28.0);

    [content addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:content.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [titleLabel.heightAnchor constraintEqualToConstant:32.0],
        [buttons.heightAnchor constraintEqualToConstant:44.0]
    ]];

    [panel center];
    return panel;
}

static AppUI::Button ShowMessageOnMainThread(const std::string& text,
                                              const std::string& title,
                                              bool allowCancel)
{
    NSApplication* application = [NSApplication sharedApplication];
    [application finishLaunching];

    const NSApplicationActivationPolicy previousPolicy = application.activationPolicy;
    [application setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application activateIgnoringOtherApps:YES];

    AutoSZUWebPanelController* controller =
        [[AutoSZUWebPanelController alloc] init];
    NSPanel* panel = CreateMessagePanel(text, title, allowCancel, controller);
    [panel makeKeyAndOrderFront:nil];
    [panel orderFrontRegardless];
    [application activateIgnoringOtherApps:YES];

    const NSModalResponse response = [application runModalForWindow:panel];
    const bool cancelled = controller.didCloseWindow ||
        response == NSModalResponseCancel || response == NSModalResponseAbort;

    [panel orderOut:nil];
    panel.delegate = nil;
    [panel close];
    [controller release];
    [panel release];

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