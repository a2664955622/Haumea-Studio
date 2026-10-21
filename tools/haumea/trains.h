#pragma once

// Trains as the SA-MP client makes them. AddStaticVehicle(Ex) of an engine
// reaches CGame::CreateVehicleRuntime -> ConstructVehicleSlot (reSA-MP
// 0.3.7-R5 gamehelpers.cpp, 0x100b83d0), which runs GTA's CREATE_MISSION_TRAIN
// script command: CTrain::CreateMissionTrain (0x6F7550) with train type 5 for
// 538, 3 for 537 and 9 for 449, clockwise = rotation > 180, and the node and
// track left for the game to find. The client then binds three more vehicles
// to the carriages GTA linked (net/vehiclepool.cpp FlushQueuedVehicleCreateEntries),
// and the server reserves four ids per train (open.mp Vehicles create()).
// GTA's train type table (0x8D44F8): 3 = 537, 569, 569, 569;
// 5 = 538, 570, 570, 570; 9 = 449 alone.
//
// Where the cars end up follows CTrain::FindClosestTrackNode (0x6F6BD0),
// FindPositionOnTrackFromCoors (0x6F6CC0) and CTrain::ProcessControl, on the
// tracks read from the user's own data/Paths/tracks.dat and tracks3.dat
// (ReadAndInterpretTrackFile, 0x6F55D0); SA-MP leaves out tracks2.dat and
// tracks4.dat.

struct ObjectInst;

enum { TRAIN_MAX_CARS = 4 };

struct TrainCar
{
	int model;
	rw::V3d position;
	rw::V3d forward;	// where the car faces
};

bool IsTrainEngineModel(int model);	// 537, 538, 449
bool IsTrainCarriageModel(int model);	// 569, 570, 590
bool IsTrainModel(int model);
// The cars of the train an engine makes, engine first: 4 for 537 and 538, 1 for 449.
int GetTrainCarModels(int engineModel, int *models);
// The vehicle ids it takes on the server (4 for 537 and 538).
int GetTrainVehicleIds(int engineModel);

void LoadTrainTracks(void);	// from the game folder, after the vehicles are defined
int GetNumTrainTracks(void);	// tracks that loaded

// The client's direction for an AddStaticVehicle angle.
bool TrainAngleIsClockwise(float angle);
// The angle to store for one read from a file: the same direction in
// [0, 360), which is how a train's angle is written back.
float TrainImportAngle(float angle);
float TrainExportAngle(float angle);

// The cars of a train spawned at position with angle, as the client places
// them; 0 without tracks. offTrack: how far (2D) the engine lands from position.
int ComputeTrainCars(int engineModel, rw::V3d position, float angle, TrainCar *cars, float *offTrack = nil);
// Where an engine put at position stands on the track (its position as the
// client places it), for placing and dragging. False without tracks.
bool SnapTrainToTrack(int engineModel, rw::V3d position, float angle, rw::V3d *snapped);

// A matrix for a car.
void TrainCarMatrix(const TrainCar &car, rw::Matrix *matrix);
float GetTrainInstAngle(const ObjectInst *inst);

// Drawing: the engine at its place on the track and its carriages behind it,
// in the world and pick passes (carriages pick as the engine).
// RenderInst calls it for an engine: puts it where the client does and
// returns the carriages to draw after it.
int UpdateTrainVisual(ObjectInst *engine, ObjectInst **carriages);
void RenderTrainPlacementGhost(int engineModel, rw::V3d position, float angle);
void ReleaseTrainCars(ObjectInst *engine);
// The tracks, faintly, while a train is selected or being placed.
void RenderTrainTracksOverlay(void);
