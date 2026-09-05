#pragma once

namespace InputDevicesCard {

// Shared input-device UI used by every game page. Device state and routing
// remain in aim_type, so adding a new game does not duplicate driver logic.
void Draw();

} // namespace InputDevicesCard
