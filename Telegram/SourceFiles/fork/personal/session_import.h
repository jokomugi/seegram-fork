#pragma once

namespace Main {
class Domain;
} // namespace Main

namespace Ui {
class RpWidget;
class Show;
class VerticalLayout;
} // namespace Ui

namespace Fork::Personal::SessionImport {

[[nodiscard]] Ui::VerticalLayout *CreateLinks(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Domain*> domain,
	std::shared_ptr<Ui::Show> show);
void ChooseTdata(Fn<void(QString)> done);

} // namespace Fork::Personal::SessionImport
