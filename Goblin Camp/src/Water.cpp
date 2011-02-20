/* Copyright 2010-2011 Ilkka Halila
This file is part of Goblin Camp.

Goblin Camp is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Goblin Camp is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License 
along with Goblin Camp. If not, see <http://www.gnu.org/licenses/>.*/
#include "stdafx.hpp"

#include <cmath>

#include <libtcod.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/weak_ptr.hpp>

#include "Random.hpp"
#include "Logger.hpp"
#include "Water.hpp"
#include "Game.hpp"
#include "Map.hpp"
#include "GCamp.hpp"
#include "Coordinate.hpp"

WaterNode::WaterNode(int vx, int vy, int vdepth, int time) :
	x(vx), y(vy), depth(vdepth),
	graphic('?'), color(TCODColor(0,128,255)),
	inertCounter(0), inert(false),
	timeFromRiverBed(time),
	filth(0),
	coastal(false)
{
	UpdateGraphic();
}

WaterNode::~WaterNode() {}

void WaterNode::Update() {
	double divided;

	if (inert) {
		++inertCounter;
	}

	if (!inert || inertCounter > (UPDATES_PER_SECOND*1)) {

		if (Map::Inst()->Type(x,y) == TILERIVERBED) {
			timeFromRiverBed = 1000;
			if (depth < RIVERDEPTH) depth = RIVERDEPTH;
		}

		inertCounter = 0;

		if (depth > 1) {

			if (timeFromRiverBed == 0 && Random::Generate(100) == 0) depth -= 1; //Evaporation
			if (timeFromRiverBed > 0 && depth < RIVERDEPTH) depth += 10; //Water rushing from the river

			std::vector<boost::weak_ptr<WaterNode> > waterList;
			std::vector<Coordinate> coordList;
			int depthSum = 0;

			//Check if any of the surrounding tiles are low, this only matters if this tile is not low
			bool onlyLowTiles = false;
			if (!Map::Inst()->IsLow(x,y)) {
				for (int ix = x-1; ix <= x+1; ++ix) {
					for (int iy = y-1; iy <= y+1; ++iy) {
						if (ix >= 0 && ix < Map::Inst()->Width() && iy >= 0 && iy < Map::Inst()->Height()) {
							if ((ix != x || iy != y) && Map::Inst()->IsLow(ix,iy)) { 
								onlyLowTiles = true;
								break;
							}
						} else if (filth > 0) { //Filth dissipates at borderwaters
							--filth;
						}
					}
				}
			}

			coastal = false; //Have to always check if this water is coastal, terrain can change
			for (int ix = x-1; ix <= x+1; ++ix) {
				for (int iy = y-1; iy <= y+1; ++iy) {
					if (ix >= 0 && ix < Map::Inst()->Width() && iy >= 0 && iy < Map::Inst()->Height()) {
						if (!coastal) {
							TileType tile = Map::Inst()->Type(ix,iy);
							if (tile != TILENONE && tile != TILEDITCH && tile != TILERIVERBED) coastal = true;
						}
						/*Choose the surrounding tiles that:
						Are the same height or low
						or in case of [onlyLowTiles] are low
						depth > RIVERDEPTH*3 at which point it can overflow upwards*/
						if (((!onlyLowTiles && Map::Inst()->IsLow(x,y) == Map::Inst()->IsLow(ix,iy)) || depth > RIVERDEPTH*3 || Map::Inst()->IsLow(ix,iy)) && !Map::Inst()->BlocksWater(ix,iy)) {
							//If we're choosing only low tiles, then this tile should be ignored completely
							if (!onlyLowTiles || (ix != x || iy != y)) {
								waterList.push_back(Map::Inst()->GetWater(ix,iy));
								coordList.push_back(Coordinate(ix,iy));
								if (waterList.back().lock()) depthSum += waterList.back().lock()->depth;
							}
						}
					}
				}
			}

			if (timeFromRiverBed > 0) --timeFromRiverBed;
			divided = ((double)depthSum/waterList.size());

			//Loop through neighbouring waternodes
			for (unsigned int i = 0; i < waterList.size(); ++i) {
				if (boost::shared_ptr<WaterNode> water = waterList[i].lock()) {
					water->depth = (int)divided;
					water->timeFromRiverBed = timeFromRiverBed;
					water->UpdateGraphic();

					//So much filth it'll go anywhere
					if (filth > 10 && Random::Generate(3) == 0) { filth -= 5; water->filth += 5; }
					//Filth goes with the flow
					if (filth > 0) {
						switch (Map::Inst()->GetFlow(x, y)) {
						case NORTH:
							if (coordList[i].Y() < y) {
								--filth;
								++water->filth;
							}
							break;

						case NORTHEAST:
							if (coordList[i].Y() < y && coordList[i].X() > x) {
								--filth;
								++water->filth;
							}
							break;

						case EAST:
							if (coordList[i].X() > x) {
								--filth;
								++water->filth;
							}
							break;

						case SOUTHEAST:
							if (coordList[i].Y() > y && coordList[i].X() > x) {
								--filth;
								++water->filth;
							}
							break;

						case SOUTH:
							if (coordList[i].Y() > y) {
								--filth;
								++water->filth;
							}
							break;

						case SOUTHWEST:
							if (coordList[i].Y() > y && coordList[i].X() < x) {
								--filth;
								++water->filth;
							}
							break;

						case WEST:
							if (coordList[i].X() < x) {
								--filth;
								++water->filth;
							}
							break;

						case NORTHWEST:
							if (coordList[i].Y() < y && coordList[i].X() < x) {
								--filth;
								++water->filth;
							}
							break;

						default:
							if (water->filth < filth && Random::GenerateBool()) {
								--filth;
								++water->filth;
							}
							break;
						}
					}
				}
				else {
					Game::Inst()->CreateWater(coordList[i], (int)divided, timeFromRiverBed);
				}
			}

			if (onlyLowTiles) {
				depth = 1; //All of the water has flown to a low tile
			}

		} else {
			int soakage = 500;
			TileType type = Map::Inst()->Type(x,y);
			if (type == TILEGRASS) soakage = 10;
			else if (type == TILEBOG) soakage = 0;
			if (Random::Generate(soakage) == 0) {
				depth = 0;
				Game::Inst()->RemoveWater(Coordinate(x,y)); //Water has evaporated
			}
		}
	}
}

void WaterNode::MakeInert() {inert = true;}
void WaterNode::DeInert() {inert = false;}
int WaterNode::Depth() {return depth;}

void WaterNode::Depth(int newDepth) {
	depth = newDepth;
	UpdateGraphic();
}

void WaterNode::UpdateGraphic() {
	if (depth == 0) {
		graphic = ' ';
	} else if (depth == 2) {
		graphic = TCOD_CHAR_BLOCK3;
	} else if (depth == 1) {
		graphic = '.';
	} else {
		graphic = 219;
	}

	int col = std::max(255-(int)(depth/25),140);
	if (color.b < std::max(col-(filth*20), 0)) ++color.b;
	if (color.b > std::max(col-(filth*20), 0)) --color.b;

	if (color.g < std::max(col/4, std::min(filth*10,150))) ++color.g;
	if (color.g > std::max(col/4, std::min(filth*10,150))) --color.g;

	if (color.r < std::min(filth*10,190)) color.r += 10;
	if (color.r > std::min(filth*10,190)) color.r -= 10;

	if (Random::Generate(39) == 0 && color.b < 200) color.b += 20;
	if (Random::Generate(9999) == 0 && color.g < 225) color.g += Random::Generate(24);
}

Coordinate WaterNode::Position() {return Coordinate(x,y);}

void WaterNode::AddFilth(int newFilth) { filth += newFilth; }
int WaterNode::GetFilth() { return filth; }

int WaterNode::GetGraphic()
{
	return graphic;
}

TCODColor WaterNode::GetColor()
{
	return color;
}

bool WaterNode::IsCoastal() { return coastal; }
