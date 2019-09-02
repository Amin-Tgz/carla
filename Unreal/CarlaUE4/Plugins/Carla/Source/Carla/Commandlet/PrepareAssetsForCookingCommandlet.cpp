// Copyright (c) 2019 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "PrepareAssetsForCookingCommandlet.h"

#include "Editor/ContentBrowser/Private/ContentBrowserUtils.h"
#include "GameFramework/WorldSettings.h"

#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"

#include "UObject/MetaData.h"

UPrepareAssetsForCookingCommandlet::UPrepareAssetsForCookingCommandlet()
{
  IsClient = false;
  IsEditor = true;
  IsServer = false;
  LogToConsole = true;

#if WITH_EDITORONLY_DATA
  static ConstructorHelpers::FObjectFinder<UMaterial> MarkingNode(TEXT(
      "Material'/Game/Carla/Static/GenericMaterials/LaneMarking/M_MarkingLane_W.M_MarkingLane_W'"));
  static ConstructorHelpers::FObjectFinder<UMaterial> RoadNode(TEXT(
      "Material'/Game/Carla/Static/GenericMaterials/Masters/LowComplexity/M_Road1.M_Road1'"));
  static ConstructorHelpers::FObjectFinder<UMaterial> RoadNodeAux(TEXT(
      "Material'/Game/Carla/Static/GenericMaterials/LaneMarking/M_MarkingLane_Y.M_MarkingLane_Y'"));
  static ConstructorHelpers::FObjectFinder<UMaterial> TerrainNodeMaterial(TEXT(
      "Material'/Game/Carla/Static/GenericMaterials/Grass/M_Grass01.M_Grass01'"));

  MarkingNodeMaterial = (UMaterial *) MarkingNode.Object;
  RoadNodeMaterial = (UMaterial *) RoadNode.Object;
  MarkingNodeMaterialAux = (UMaterial *) RoadNodeAux.Object;
#endif
}
#if WITH_EDITORONLY_DATA

// NOTE: Assets imported from a map FBX will be classified for semantic
// segmentation as OTHER, ROAD, ROADLINES AND TERRAIN based on the asset name.
// Note that if the asset name contains Marking, it will classify it as
// RoadLines tag in Carla. If it is not possible to classify the asset name,
// then we will use OTHER tag in Carla.
namespace SSTags {
  // Carla Tags
  static const FString OTHER      = TEXT("Other");
  static const FString ROAD       = TEXT("Roads");
  static const FString ROADLINES  = TEXT("RoadLines");
  static const FString VEGETATION = TEXT("Vegetation");

  // RoadRunner Tags
  static const FString TERRAIN    = TEXT("Terrain");
  static const FString MARKING    = TEXT("Marking");
}

FPackageParams UPrepareAssetsForCookingCommandlet::ParseParams(const FString &InParams) const
{
  TArray<FString> Tokens;
  TArray<FString> Params;
  TMap<FString, FString> ParamVals;

  ParseCommandLine(*InParams, Tokens, Params);

  FPackageParams PackageParams;
  FParse::Value(*InParams, TEXT("PackageName="), PackageParams.Name);
  FParse::Bool(*InParams, TEXT("OnlyPrepareMaps="), PackageParams.bOnlyPrepareMaps);
  FParse::Bool(*InParams, TEXT("OnlyMoveMeshes="), PackageParams.bOnlyMoveMeshes);
  return PackageParams;
}

void UPrepareAssetsForCookingCommandlet::LoadWorld(FAssetData &AssetData)
{
  // BaseMap path inside Carla
  FString BaseMap = TEXT("/Game/Carla/Maps/BaseMap");

  // Load Map folder using object library
  MapObjectLibrary = UObjectLibrary::CreateLibrary(UWorld::StaticClass(), false, GIsEditor);
  MapObjectLibrary->AddToRoot();
  MapObjectLibrary->LoadAssetDataFromPath(*BaseMap);
  MapObjectLibrary->LoadAssetsFromAssetData();
  MapObjectLibrary->GetAssetDataList(AssetDatas);

  if (AssetDatas.Num() > 0)
  {
    // Extract first asset found in folder path (i.e. the BaseMap)
    AssetData = AssetDatas.Pop();
  }
}

TArray<AStaticMeshActor *> UPrepareAssetsForCookingCommandlet::SpawnMeshesToWorld(
    const TArray<FString> &AssetsPaths,
    bool bUseCarlaMaterials)
{
  TArray<AStaticMeshActor *> SpawnedMeshes;
  // Load assets specified in AssetsPaths by using an object library
  // for building map world

  AssetsObjectLibrary = UObjectLibrary::CreateLibrary(UStaticMesh::StaticClass(), false, GIsEditor);
  AssetsObjectLibrary->AddToRoot();

  AssetsObjectLibrary->LoadAssetDataFromPaths(AssetsPaths);
  AssetsObjectLibrary->LoadAssetsFromAssetData();
  MapContents.Empty();
  AssetsObjectLibrary->GetAssetDataList(MapContents);

  // Create default Transform for all assets to spawn
  const FTransform zeroTransform = FTransform();
  FVector initialVector = FVector(0, 0, 0);
  FRotator initialRotator = FRotator(0, 180, 0);

  UStaticMesh *MeshAsset;
  AStaticMeshActor *MeshActor;

  for (auto MapAsset : MapContents)
  {
    // Spawn Static Mesh
    MeshAsset = CastChecked<UStaticMesh>(MapAsset.GetAsset());
    MeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
        initialVector,
        initialRotator);
    MeshActor->GetStaticMeshComponent()->SetStaticMesh(CastChecked<UStaticMesh>(MeshAsset));
    SpawnedMeshes.Add(MeshActor);
    if (bUseCarlaMaterials)
    {
      // Set Carla Materials
      FString AssetName;
      MapAsset.AssetName.ToString(AssetName);
      if (AssetName.Contains(SSTags::MARKING))
      {
        MeshActor->GetStaticMeshComponent()->SetMaterial(0, MarkingNodeMaterial);
        MeshActor->GetStaticMeshComponent()->SetMaterial(1, MarkingNodeMaterialAux);
      }
      else if (AssetName.Contains(SSTags::ROAD))
      {
        MeshActor->GetStaticMeshComponent()->SetMaterial(0, RoadNodeMaterial);
      }
      else if (AssetName.Contains(SSTags::TERRAIN))
      {
        MeshActor->GetStaticMeshComponent()->SetMaterial(0, TerrainNodeMaterial);
      }
    }
  }

  // Clear loaded assets in library
  AssetsObjectLibrary->ClearLoaded();

  // Mark package dirty
  World->MarkPackageDirty();

  return SpawnedMeshes;
}

void UPrepareAssetsForCookingCommandlet::DestroySpawnedActorsInWorld(
    TArray<AStaticMeshActor *> &SpawnedActors)
{
  // Destroy all spawned actors
  for (auto Actor : SpawnedActors)
  {
    Actor->Destroy();
  }

  // Mark package dirty
  World->MarkPackageDirty();
}

bool UPrepareAssetsForCookingCommandlet::SaveWorld(
    FAssetData &AssetData,
    const FString &PackageName,
    const FString &DestPath,
    const FString &WorldName)
{
  // Create Package to save
  UPackage *Package = AssetData.GetPackage();
  Package->SetFolderName(*DestPath);
  Package->FullyLoad();
  Package->MarkPackageDirty();
  FAssetRegistryModule::AssetCreated(World);

  // Renaming map
  World->Rename(*WorldName, World->GetOuter());
  FString PackagePath = DestPath + "/" + WorldName;
  FAssetRegistryModule::AssetRenamed(World, *PackagePath);
  World->MarkPackageDirty();
  World->GetOuter()->MarkPackageDirty();

  // Check if OpenDrive file exists
  FString PathXODR = FPaths::ProjectContentDir() + PackageName + TEXT("/Maps/") + WorldName + TEXT(
      "/OpenDrive/") + WorldName + TEXT(".xodr");

  bool bPackageSaved = false;
  if (FPaths::FileExists(PathXODR))
  {
    // We need to spawn OpenDrive assets before saving the map
    AOpenDriveActor *OpenWorldActor =
        CastChecked<AOpenDriveActor>(World->SpawnActor(AOpenDriveActor::StaticClass(),
        new FVector(), NULL));

    OpenWorldActor->BuildRoutes(WorldName);
    OpenWorldActor->AddSpawners();

    SavePackage(PackagePath, Package);

    // We need to destroy OpenDrive assets once saved the map
    OpenWorldActor->RemoveRoutes();
    OpenWorldActor->RemoveSpawners();
    OpenWorldActor->Destroy();
  }
  else
  {
    SavePackage(PackagePath, Package);
  }
  return bPackageSaved;
}

FString UPrepareAssetsForCookingCommandlet::GetFirstPackagePath(const FString &PackageName) const
{
  // Get all Package names
  TArray<FString> PackageList;
  IFileManager::Get().FindFilesRecursive(PackageList, *(FPaths::ProjectContentDir()),
      *(PackageName + TEXT(".Package.json")), true, false, false);

  if (PackageList.Num() == 0)
  {
    UE_LOG(LogTemp, Error, TEXT("Package json file not found."));
    return {};
  }

  return IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*PackageList[0]);
}

FAssetsPaths UPrepareAssetsForCookingCommandlet::GetAssetsPathFromPackage(const FString &PackageName) const
{
  FString PackageJsonFilePath = GetFirstPackagePath(PackageName);

  FAssetsPaths AssetsPaths;

  // Get All Maps Path
  FString MapsFileJsonContent;
  if (FFileHelper::LoadFileToString(MapsFileJsonContent, *PackageJsonFilePath))
  {
    TSharedPtr<FJsonObject> JsonParsed;
    TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(MapsFileJsonContent);
    if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
    {
      // Add Maps Path
      auto MapsJsonArray = JsonParsed->GetArrayField(TEXT("maps"));

      for (auto &MapJsonValue : MapsJsonArray)
      {
        TSharedPtr<FJsonObject> MapJsonObject = MapJsonValue->AsObject();

        FMapData MapData;
        MapData.Name = MapJsonObject->GetStringField(TEXT("name"));
        MapData.Path = MapJsonObject->GetStringField(TEXT("path"));
        MapData.bUseCarlaMapMaterials = MapJsonObject->GetBoolField(TEXT("use_carla_materials"));

        AssetsPaths.MapsPaths.Add(std::move(MapData));
      }

      // Add Props Path
      auto PropJsonArray = JsonParsed->GetArrayField(TEXT("props"));

      for (auto &PropJsonValue : PropJsonArray)
      {
        TSharedPtr<FJsonObject> PropJsonObject = PropJsonValue->AsObject();

        FString PropAssetPath = PropJsonObject->GetStringField(TEXT("path"));

        AssetsPaths.PropsPaths.Add(PropAssetPath);
      }
    }
  }
  return AssetsPaths;
}

bool SaveStringTextToFile(
    FString SaveDirectory,
    FString FileName,
    FString SaveText,
    bool bAllowOverWriting)
{
  IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

  // CreateDirectoryTree returns true if the destination
  // directory existed prior to call or has been created
  // during the call.
  if (PlatformFile.CreateDirectoryTree(*SaveDirectory))
  {
    // Get absolute file path
    FString AbsoluteFilePath = SaveDirectory + "/" + FileName;

    // Allow overwriting or file doesn't already exist
    if (bAllowOverWriting || !PlatformFile.FileExists(*AbsoluteFilePath))
    {
      FFileHelper::SaveStringToFile(SaveText, *AbsoluteFilePath);
    }
  }
  return true;
}

bool UPrepareAssetsForCookingCommandlet::SavePackage(const FString &PackagePath, UPackage *Package) const
{
  FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath,
      FPackageName::GetMapPackageExtension());

  if (FPaths::FileExists(*PackageFileName))
  {
    // Will not save package if it already exists
    return false;
  }
  return UPackage::SavePackage(Package, World, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone,
      *PackageFileName, GError, nullptr, true, true, SAVE_NoError);
}

void UPrepareAssetsForCookingCommandlet::GenerateMapPathsFile(
    const FAssetsPaths &AssetsPaths,
    const FString &PropsMapPath)
{
  FString MapPathData;
  for (const auto &Map : AssetsPaths.MapsPaths)
  {
    MapPathData.Append(Map.Path + TEXT("/") + Map.Name + TEXT("+"));
  }

  if (PropsMapPath.IsEmpty())
  {
    MapPathData.Append(PropsMapPath);
  }
  else
  {
    if (!MapPathData.IsEmpty())
    {
      MapPathData.RemoveFromEnd(TEXT("+"));
    }
  }

  FString SaveDirectory = FPaths::ProjectContentDir();
  FString FileName = FString("MapPaths.txt");
  SaveStringTextToFile(SaveDirectory, FileName, MapPathData, true);
}

void UPrepareAssetsForCookingCommandlet::GeneratePackagePathFile(const FString &PackageName)
{
  FString SaveDirectory = FPaths::ProjectContentDir();
  FString FileName = FString("PackagePath.txt");
  FString PackageJsonFilePath = GetFirstPackagePath(PackageName);
  SaveStringTextToFile(SaveDirectory, FileName, PackageJsonFilePath, true);
}

void UPrepareAssetsForCookingCommandlet::MoveMeshes(
    const FString &PackageName,
    const TArray<FMapData> &MapsPaths)
{

  MoveAssetsObjectLibrary = UObjectLibrary::CreateLibrary(UStaticMesh::StaticClass(), false, GIsEditor);
  MoveAssetsObjectLibrary->AddToRoot();

  for (const auto &Map : MapsPaths)
  {
    MoveMeshesForSemanticSegmentation(PackageName, Map.Name);
  }
}

void UPrepareAssetsForCookingCommandlet::PrepareMapsForCooking(
    const FString &PackageName,
    const TArray<FMapData> &MapsPaths)
{
  // Load World
  FAssetData AssetData;
  LoadWorld(AssetData);
  World = CastChecked<UWorld>(AssetData.GetAsset());

  FString BasePath = TEXT("/Game/") + PackageName + TEXT("/Static/");

  for (const auto &Map : MapsPaths)
  {
    FString MapPath = TEXT("/") + Map.Name;

    FString OtherPath     = BasePath + SSTags::OTHER      + MapPath;
    FString RoadsPath     = BasePath + SSTags::ROAD       + MapPath;
    FString RoadLinesPath = BasePath + SSTags::ROADLINES  + MapPath;
    FString VegetationPath   = BasePath + SSTags::VEGETATION    + MapPath;

    // Spawn assets located in semantic segmentation fodlers
    TArray<FString> DataPath = {OtherPath, RoadsPath, RoadLinesPath, VegetationPath};

    TArray<AStaticMeshActor *> SpawnedActors = SpawnMeshesToWorld(DataPath, Map.bUseCarlaMapMaterials);

    // Save the World in specified path
    SaveWorld(AssetData, PackageName, Map.Path, Map.Name);

    // Remove spawned actors from world to keep equal as BaseMap
    DestroySpawnedActorsInWorld(SpawnedActors);
  }
}

void UPrepareAssetsForCookingCommandlet::PreparePropsForCooking(
    FString &PackageName,
    const TArray<FString> &PropsPaths,
    FString &MapDestPath)
{
  // Load World
  FAssetData AssetData;
  LoadWorld(AssetData);
  World = CastChecked<UWorld>(AssetData.GetAsset());

  // Remove the meshes names from the original path for props, so we can load
  // props inside folder
  TArray<FString> PropPathDirs = PropsPaths;

  for (auto &PropPath : PropPathDirs)
  {
    PropPath.Split(
        TEXT("/"),
        &PropPath,
        nullptr,
        ESearchCase::Type::IgnoreCase,
        ESearchDir::Type::FromEnd);
  }

  // Add props in a single Base Map
  TArray<AStaticMeshActor *> SpawnedActors = SpawnMeshesToWorld(PropPathDirs, false);

  FString MapName("PropsMap");
  SaveWorld(AssetData, PackageName, MapDestPath, MapName);

  DestroySpawnedActorsInWorld(SpawnedActors);
  MapObjectLibrary->ClearLoaded();
}

void UPrepareAssetsForCookingCommandlet::MoveMeshesForSemanticSegmentation(
    const FString &PackageName,
    const FString &MapName)
{
  // Prepare a UObjectLibrary for moving assets
  const FString SrcPath = TEXT("/Game/") + PackageName + TEXT("/Maps/") + MapName;
  MoveAssetsObjectLibrary->LoadAssetDataFromPath(*SrcPath);
  MoveAssetsObjectLibrary->LoadAssetsFromAssetData();

  // Load Assets to move
  MoveMapContents.Empty();
  MoveAssetsObjectLibrary->GetAssetDataList(MoveMapContents);
  MoveAssetsObjectLibrary->ClearLoaded();

  TArray<FString> DestinationPaths = {SSTags::OTHER, SSTags::ROAD, SSTags::ROADLINES, SSTags::VEGETATION};

  // Init Map with keys
  TMap<FString, TArray<UObject *>> AssetDataMap;
  for (const auto &Paths : DestinationPaths)
  {
    AssetDataMap.Add(Paths, {});
  }

  for (const auto &MapAsset : MoveMapContents)
  {
    // Get AssetName
    UStaticMesh *MeshAsset = CastChecked<UStaticMesh>(MapAsset.GetAsset());
    FString ObjectName = MeshAsset->GetName();

    FString AssetName;
    MapAsset.AssetName.ToString(AssetName);

    if (SrcPath.Len())
    {

      const FString CurrentPackageName = MeshAsset->GetOutermost()->GetName();

      if (!ensure(CurrentPackageName.StartsWith(SrcPath)))
      {
        continue;
      }

      // Classify in different folders according to semantic segmentation
      if (AssetName.Contains(SSTags::ROAD))
      {
        AssetDataMap[SSTags::ROAD].Add(MeshAsset);
      }
      else if (AssetName.Contains(SSTags::MARKING))
      {
        AssetDataMap[SSTags::ROADLINES].Add(MeshAsset);
      }
      else if (AssetName.Contains(SSTags::TERRAIN))
      {
        AssetDataMap[SSTags::VEGETATION].Add(MeshAsset);
      }
      else
      {
        AssetDataMap[SSTags::OTHER].Add(MeshAsset);
      }
    }
  }

  for (const auto &Elem : AssetDataMap)
  {
    FString DestPath = TEXT("/Game/") + PackageName + TEXT("/Static/") + Elem.Key + "/" + MapName;
    ContentBrowserUtils::MoveAssets(Elem.Value, DestPath);
  }
}

int32 UPrepareAssetsForCookingCommandlet::Main(const FString &Params)
{
  FPackageParams PackageParams = ParseParams(Params);

  // Get Props and Maps Path
  FAssetsPaths AssetsPaths = GetAssetsPathFromPackage(PackageParams.Name);

  if (PackageParams.bOnlyMoveMeshes)
  {
    MoveMeshes(PackageParams.Name, AssetsPaths.MapsPaths);
  }
  else
  {
    if (PackageParams.bOnlyPrepareMaps)
    {
      PrepareMapsForCooking(PackageParams.Name, AssetsPaths.MapsPaths);
    }
    else
    {
      FString PropsMapPath("");

      if (AssetsPaths.PropsPaths.Num() > 0)
      {
        FString MapName("PropsMap");
        PropsMapPath = TEXT("/Game/") + PackageParams.Name + TEXT("/Maps/") + MapName;
        PreparePropsForCooking(PackageParams.Name, AssetsPaths.PropsPaths, MapName);
      }

      // Save Map Path File for further use
      GenerateMapPathsFile(AssetsPaths, PropsMapPath);

      // Saves Package path for further use
      GeneratePackagePathFile(PackageParams.Name);
    }
  }

  return 0;
}
#endif
