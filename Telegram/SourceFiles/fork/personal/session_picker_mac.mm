#include "fork/personal/session_import.h"

#include <Cocoa/Cocoa.h>

namespace Fork::Personal::SessionImport {

void ChooseTdata(Fn<void(QString)> done) {
	const auto panel = [NSOpenPanel openPanel];
	[panel setCanChooseDirectories:YES];
	[panel setCanChooseFiles:YES];
	[panel setAllowsMultipleSelection:NO];
	[panel setResolvesAliases:NO];
	[panel setAllowedFileTypes:@[@"zip"]];
	[panel beginWithCompletionHandler:^(NSModalResponse response) {
		const auto path = response == NSModalResponseOK ? [[panel URL] path] : nil;
		done(path ? QString::fromUtf8([path UTF8String]) : QString());
	}];
}

} // namespace Fork::Personal::SessionImport
