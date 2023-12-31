// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetCommands.h"

#define LOCTEXT_NAMESPACE "IKRetargetCommands"

void FIKRetargetCommands::RegisterCommands()
{
	UI_COMMAND(RunRetargeter, "Run Retargeter", "Resume playback of animation and retarget to the target skeleton.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(EditRetargetPose, "Edit Retarget Pose", "Enter into mode allowing manual editing of the target skeleton pose in the viewport.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ShowRetargetPose, "Show Retarget Pose", "Display the retarget pose and retarget to the target skeleton.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ShowGlobalSettings, "Global Settings", "Open global settings in detail tab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ShowRootSettings, "Root Settings", "Open root settings in detail tab.", EUserInterfaceActionType::Button, FInputChord());
	
	UI_COMMAND(ResetAllBones, "Reset All", "Sets the selected bones to the mesh reference pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetSelectedBones, "Reset Selected Bones", "Sets the selected bones to the mesh reference pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetSelectedAndChildrenBones, "Reset Selected and Children Bones", "Sets the selected bones (and all children recursively) to the mesh reference pose.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(NewRetargetPose, "Create New", "Create a new retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DuplicateRetargetPose, "Duplicate Current", "Duplicate the current retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteRetargetPose, "Delete", "Delete current retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RenameRetargetPose, "Rename", "Rename current retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ImportRetargetPose, "Import from Pose Asset", "Import a retarget pose asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ImportRetargetPoseFromAnim, "Import from Animation Sequence", "Import a retarget pose from an animation sequence.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportRetargetPose, "Export Pose Asset", "Export current retarget pose as a Pose Asset.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
