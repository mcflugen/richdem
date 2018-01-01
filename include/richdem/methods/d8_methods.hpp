/**
  @file
  @brief Defines a number of functions for calculating terrain attributes

  Richard Barnes (rbarnes@umn.edu), 2015
*/
//TODO: Curvature may be "Least Squares Fitted Plane" per http://gis.stackexchange.com/questions/37066/how-to-calculate-terrain-curvature
#ifndef _richdem_d8_methods_hpp_
#define _richdem_d8_methods_hpp_

#include "richdem/common/logger.hpp"
#include "richdem/common/Array2D.hpp"
#include "richdem/common/constants.hpp"
#include "richdem/common/grid_cell.hpp"
#include "richdem/common/ProgressBar.hpp"
#include <queue>
#include <stdexcept>

namespace richdem {

/**
  @brief  Returns the sign (+1, -1, 0) of a number. Branchless.
  @author Richard Barnes (rbarnes@umn.edu)

  @param[in]  val  Input value

  @return
    -1 for a negative input, +1 for a positive input, and 0 for a zero input
*/
template <class T>
static inline T sgn(T val){
  return (T(0) < val) - (val < T(0));
}






/**
  @brief  Calculates the D8 flow accumulation, given the D8 flow directions
  @author Richard Barnes (rbarnes@umn.edu)

  This calculates the D8 flow accumulation of a grid of D8 flow directions by
  calculating each cell's dependency on its neighbours and then using a
  priority-queue to process cells in a top-of-the-watershed-down fashion

  @param[in]  &flowdirs  A D8 flowdir grid from d8_flow_directions()
  @param[out] &area      Returns the up-slope area of each cell
*/
template<class T, class U>
void d8_flow_accum(const Array2D<T> &flowdirs, Array2D<U> &area){
  std::queue<GridCell> sources;
  ProgressBar progress;

  RDLOG_ALG_NAME<<"D8 Flow Accumulation";
  RDLOG_CITATION<<"TODO";

  RDLOG_MEM_USE<<"The sources queue will require at most approximately "
               <<(flowdirs.size()*((long)sizeof(GridCell))/1024/1024)
               <<"MB of RAM.";

  RDLOG_PROGRESS<<"Resizing dependency matrix...";
  Array2D<int8_t> dependency(flowdirs,0);

  RDLOG_PROGRESS<<"Setting up the area matrix...";
  area.resize(flowdirs,0);
  area.setNoData(-1);

  RDLOG_PROGRESS<<"Calculating dependency matrix & setting noData() cells...";
  progress.start( flowdirs.size() );
  #pragma omp parallel for
  for(int y=0;y<flowdirs.height();y++){
    progress.update( y*flowdirs.width() );
    for(int x=0;x<flowdirs.width();x++){
      if(flowdirs.isNoData(x,y)){
        area(x,y) = area.noData();
        continue;
      }

      int n = flowdirs(x,y); //The neighbour this cell flows into
      if(n==NO_FLOW)         //This cell does not flow into a neighbour
        continue;

      int nx = x+dx[n];      //x-coordinate of the neighbour
      int ny = y+dy[n];      //y-coordinate of the neighbour

      //Neighbour is not on the grid
      if(!flowdirs.inGrid(nx,ny))
        continue;

      //Neighbour is valid and is part of the grid. The neighbour depends on this
      //cell, so increment its dependency count.
      ++dependency(nx,ny);
    }
  }
  RDLOG_TIME_USE<<"Dependency calculation time = "<<progress.stop()<<" s";

  RDLOG_PROGRESS<<"Locating source cells...";
  for(int y=0;y<flowdirs.height();y++)
  for(int x=0;x<flowdirs.width();x++)
    if(dependency(x,y)==0 && !flowdirs.isNoData(x,y))
      sources.emplace(x,y);

  RDLOG_PROGRESS<<"Calculating flow accumulation areas...";
  progress.start(flowdirs.numDataCells());
  long int ccount=0;
  while(sources.size()>0){
    GridCell c=sources.front();
    sources.pop();

    ccount++;
    progress.update(ccount);

    area(c.x,c.y)++;

    int n = flowdirs(c.x,c.y);

    if(n==NO_FLOW)
      continue;

    int nx=c.x+dx[n];
    int ny=c.y+dy[n];

    if(!flowdirs.inGrid(nx,ny))
      continue;
    if(flowdirs.isNoData(nx,ny))
      continue;

    area(nx,ny)+=area(c.x,c.y);
    --dependency(nx,ny);

    if(dependency(nx,ny)==0)
      sources.emplace(nx,ny);
  }
  RDLOG_TIME_USE<<"Flow accumulation calculation time = "<<progress.stop()<<" s";

  //TODO: Explain this better
  int loops=0;
  for(int i=-1;i>=-8;i--)
    loops+=dependency.countval(-1);
  RDLOG_MISC<<"Input contained at least = "<<loops<<" loops";
}




//d8_upslope_cells
/**
  @brief  Calculates which cells ultimately D8-flow through a given cell
  @author Richard Barnes (rbarnes@umn.edu)

  Given the coordinates x0,y0 of a cell and x1,y1 of another, possibly distinct,
  cell this draws a line between the two using the Bresenham Line-Drawing
  Algorithm and returns a grid showing all the cells whose flow ultimately
  passes through the indicated cells.

  The grid has the values:

  * 1=Upslope cell
  * 2=Member of initializing line
  * All other cells have a noData() value

  @param[in]  x0              x-coordinate of start of line
  @param[in]  y0              y-coordinate of start of line
  @param[in]  x1              x-coordinate of end of line
  @param[in]  y1              y-coordinate of end of line
  @param[in]  &flowdirs       A D8 flowdir grid from d8_flow_directions()
  @param[out] &upslope_cells  A grid of 1/2/NoData, as in the description
*/
template<class T, class U>
void d8_upslope_cells(
  int x0,
  int y0,
  int x1,
  int y1,
  const Array2D<T> &flowdirs,
  Array2D<U>       &upslope_cells
){
  //TODO: ALG NAME?
  RDLOG_PROGRESS<<"Setting up the upslope_cells matrix..."<<std::flush;
  upslope_cells.resize(flowdirs);
  upslope_cells.setAll(FLOWDIR_NO_DATA);
  upslope_cells.setNoData(FLOWDIR_NO_DATA);

  ProgressBar progress;

  std::queue<GridCell> expansion;

  if(x0>x1){
    std::swap(x0,x1);
    std::swap(y0,y1);
  }

  //Modified Bresenham Line-Drawing Algorithm
  int deltax     = x1-x0;
  int deltay     = y1-y0;
  float error    = 0;
  float deltaerr = (float)deltay/(float)deltax;

  if (deltaerr<0)
    deltaerr = -deltaerr;

  RDLOG_MISC<<"Line slope is "<<deltaerr;
  int y=y0;
  for(int x=x0;x<=x1;x++){
    expansion.push(GridCell(x,y));
    upslope_cells(x,y)=2;
    error+=deltaerr;
    if (error>=0.5) {
      expansion.push(GridCell(x+1,y));
      upslope_cells(x+1,y) = 2;
      y                   += sgn(deltay);
      error               -= 1;
    }
  }

  progress.start(flowdirs.data_cells);
  long int ccount=0;
  while(expansion.size()>0){
    GridCell c=expansion.front();
    expansion.pop();

    progress.update(ccount++);

    for(int n=1;n<=8;n++)
      if(!flowdirs.inGrid(c.x+dx[n],c.y+dy[n]))
        continue;
      else if(flowdirs(c.x+dx[n],c.y+dy[n])==NO_FLOW)
        continue;
      else if(flowdirs(c.x+dx[n],c.y+dy[n])==flowdirs.noData())
        continue;
      else if(upslope_cells(c.x+dx[n],c.y+dy[n])==upslope_cells.noData() && n==d8_inverse[flowdirs(c.x+dx[n],c.y+dy[n])]){
        expansion.push(GridCell(c.x+dx[n],c.y+dy[n]));
        upslope_cells(c.x+dx[n],c.y+dy[n])=1;
      }
  }
  RDLOG_TIME_USE<<"Succeeded in "<<progress.stop();
  RDLOG_MISC<<"Found "<<ccount<<" up-slope cells."; //TODO
}




//d8_SPI
/**
  @brief  Calculates the SPI terrain attribute
  @author Richard Barnes (rbarnes@umn.edu)

  \f$(\textit{CellSize}\cdot\textit{FlowAccumulation}+0.001)\cdot(\frac{1}{100}\textit{PercentSlope}+0.001)\f$

  @param[in]   &flow_accumulation   A flow accumulation grid (dinf_upslope_area())
  @param[in]   &riserun_slope       A percent_slope grid (d8_slope())
  @param[out]  &result              Altered to return the calculated SPI

  @pre \p flow_accumulation and \p percent_slope must be the same size

  @post \p result takes the properties and dimensions of \p flow_accumulation

  @todo Generalize for float and int grids
*/

template<class T, class U, class V>
void TA_SPI(
  const Array2D<T> &flow_accumulation,
  const Array2D<U> &riserun_slope,
        Array2D<V> &result
){
  Timer timer;

  RDLOG_ALG_NAME<<"d8_SPI";
  //TODO: CITATION

  if(flow_accumulation.width()!=riserun_slope.width() || flow_accumulation.height()!=riserun_slope.height())
    throw std::runtime_error("Couldn't calculate SPI! The input matricies were of unequal dimensions!");

  RDLOG_PROGRESS<<"Setting up the SPI matrix..."<<std::flush;
  result.resize(flow_accumulation);
  result.setNoData(-1);  //Log(x) can't take this value of real inputs, so we're good

  RDLOG_PROGRESS<<"Calculating SPI...";
  timer.start();
  #pragma omp parallel for collapse(2)
  for(int x=0;x<flow_accumulation.width();x++)
    for(int y=0;y<flow_accumulation.height();y++)
      if(flow_accumulation(x,y)==flow_accumulation.noData() || riserun_slope(x,y)==riserun_slope.noData())
        result(x,y)=result.noData();
      else
        result(x,y)=log( (flow_accumulation(x,y)/flow_accumulation.getCellArea()) * (riserun_slope(x,y)+0.001) );
  RDLOG_TIME_USE<<"succeeded in "<<timer.stop()<<"s.";
}






//d8_CTI
/**
  @brief  Calculates the CTI terrain attribute
  @author Richard Barnes (rbarnes@umn.edu)

  \f$\log{\frac{\textit{CellSize}\cdot\textit{FlowAccumulation}+0.001}{\frac{1}{100}\textit{PercentSlope}+0.001}}\f$

  @param[in]  &flow_accumulation    A flow accumulation grid (dinf_upslope_area())
  @param[in]  &riserun_slope        A percent_slope grid (d8_slope())
  @param[out] &result               Altered to return the calculated SPI

  @pre \p flow_accumulation and \p percent_slope must be the same size

  @post \p result takes the properties and dimensions of \p flow_accumulation

  @todo Generalize for float and int grids
*/
template<class T, class U, class V>
void TA_CTI(
  const Array2D<T> &flow_accumulation,
  const Array2D<U> &riserun_slope,
        Array2D<V> &result
){
  Timer timer;

  RDLOG_ALG_NAME<<"d8_CTI";

  if(flow_accumulation.width()!=riserun_slope.width() || flow_accumulation.height()!=riserun_slope.height())
    throw std::runtime_error("Couldn't calculate CTI! The input matricies were of unequal dimensions!");

  RDLOG_PROGRESS<<"Setting up the CTI matrix..."<<std::flush;
  result.resize(flow_accumulation);
  result.setNoData(-1);  //Log(x) can't take this value of real inputs, so we're good
  RDLOG_PROGRESS<<"succeeded.";

  RDLOG_PROGRESS<<"Calculating CTI..."<<std::flush;
  timer.start();
  #pragma omp parallel for collapse(2)
  for(int x=0;x<flow_accumulation.width();x++)
    for(int y=0;y<flow_accumulation.height();y++)
      if(flow_accumulation(x,y)==flow_accumulation.noData() || riserun_slope(x,y)==riserun_slope.noData())
        result(x,y)=result.noData();
      else
        result(x,y)=log( (flow_accumulation(x,y)/flow_accumulation.getCellArea()) / (riserun_slope(x,y)+0.001) );
  RDLOG_TIME_USE<<"succeeded in "<<timer.stop()<<"s.";
}



//d8_terrain_attrib_helper
/**
  @brief  Calculate a variety of terrain attributes
  @author Richard Barnes (rbarnes@umn.edu), Burrough (1998)

  This calculates a variety of terrain attributes according
  to the work of Burrough 1998's "Principles of Geographical
  Information Systems" (p. 190). Algorithms and helpful ArcGIS
  pages are noted in comments in the code.

  @param[in]  &elevations      An elevation grid
  @param[in]  x0          X coordinate of cell to perform calculation on
  @param[in]  y0          Y coordinate of cell to perform calculation on
  @param[out]  &rise_over_run
    Returns rise-over-run slope as per Horn 1981
  @param[out]  &aspect
    Returns aspect as per Horn 1981 in degrees [0,360). Degrees increase
    in a clockwise fashion. 0 is north, -1 indicates a flat surface.
  @param[out]  &curvature
    Returns the difference of profile and planform curvatures
    (TODO: Clarify, this is from ArcGIS and was poorly described)
  @param[out]  &profile_curvature
    Returns the profile curvature as per Zevenbergen and Thorne 1987.
    0 indicates a flat surface.
  @param[out]  &planform_curvature  
    Returns the planform curvature as per Zevenbergen and Thorne 1987.
    0 indicates a flat surface.

  @pre This function should never be called on a NoData cell
*/

static class TA_Setup_Vars {
 public:
  double a,b,c,d,e,f,g,h,i;
};

static class TA_Setup_Curves_Vars {
 public:
  double L,D,E,F,G,H;
};



/*
Slope derived from ArcGIS help at:
http://webhelp.esri.com/arcgiSDEsktop/9.3/index.cfm?TopicName=How%20Slope%20works
http://help.arcgis.com/en/arcgisdesktop/10.0/help/index.html#/How_Slope_works/009z000000vz000000/

Cells are identified as
Aspect derived from AcrGIS help at:
http://help.arcgis.com/en/arcgisdesktop/10.0/help/index.html#/How_Aspect_works/00q900000023000000/
http://help.arcgis.com/en/arcgisdesktop/10.0/help/index.html#/How_Aspect_works/009z000000vp000000/

Curvature dervied from ArcGIS help at:
http://help.arcgis.com/en/arcgisdesktop/10.0/help/index.html#//00q90000000t000000
http://blogs.esri.com/esri/arcgis/2010/10/27/understanding-curvature-rasters/

Expanded ArcGIS curvature info
http://support.esri.com/en/knowledgebase/techarticles/detail/21942
*/

//a b c
//d e f
//g h i
//Deal with grid edges and NoData values in the manner suggested by
//ArcGIS. Note that this function should never be called on a NoData cell

template<class T>
static inline TA_Setup_Vars TerrainSetup(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  TA_Setup_Vars tsv;
  tsv.a=tsv.b=tsv.c=tsv.d=tsv.e=tsv.f=tsv.g=tsv.h=tsv.i=elevations(x,y);
  if(elevations.inGrid(x-1,y-1) && elevations(x-1,y-1)!=elevations.noData()) tsv.a = elevations(x-1,y-1);
  if(elevations.inGrid(x-1,y  ) && elevations(x-1,y  )!=elevations.noData()) tsv.d = elevations(x-1,y  );
  if(elevations.inGrid(x-1,y+1) && elevations(x-1,y+1)!=elevations.noData()) tsv.g = elevations(x-1,y+1);
  if(elevations.inGrid(x  ,y-1) && elevations(x,  y-1)!=elevations.noData()) tsv.b = elevations(x,  y-1);
  if(elevations.inGrid(x  ,y+1) && elevations(x,  y+1)!=elevations.noData()) tsv.h = elevations(x,  y+1);
  if(elevations.inGrid(x+1,y-1) && elevations(x+1,y-1)!=elevations.noData()) tsv.c = elevations(x+1,y-1);
  if(elevations.inGrid(x+1,y  ) && elevations(x+1,y  )!=elevations.noData()) tsv.f = elevations(x+1,y  );
  if(elevations.inGrid(x+1,y+1) && elevations(x+1,y+1)!=elevations.noData()) tsv.i = elevations(x+1,y+1);

  tsv.a *= zscale;
  tsv.b *= zscale;
  tsv.c *= zscale;
  tsv.d *= zscale;
  tsv.e *= zscale;
  tsv.f *= zscale;
  tsv.g *= zscale;
  tsv.h *= zscale;
  tsv.i *= zscale;

  return tsv;
}

template<class T>
static inline TA_Setup_Curves_Vars TerrainCurvatureSetup(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const TA_Setup_Vars tsv = TerrainSetup(elevations, x, y, zscale);

  TA_Setup_Curves_Vars tscv;
  //Z1 Z2 Z3   a b c
  //Z4 Z5 Z6   d e f
  //Z7 Z8 Z9   g h i
  //Curvatures in the manner of Zevenbergen and Thorne 1987

  tscv.L  = elevations.getCellLengthX();
  tscv.D  = ( (tsv.d+tsv.f)/2 - tsv.e) / tscv.L / tscv.L;  //D = [(Z4 + Z6) /2 - Z5] / L^2
  tscv.E  = ( (tsv.b+tsv.h)/2 - tsv.e) / tscv.L / tscv.L;  //E = [(Z2 + Z8) /2 - Z5] / L^2
  tscv.F  = (-tsv.a+tsv.c+tsv.g-tsv.i)/4/tscv.L/tscv.L;    //F = (-Z1+Z3+Z7-Z9)/(4L^2)
  tscv.G  = (-tsv.d+tsv.f)/2/tscv.L;                       //G = (-Z4+Z6)/(2L)
  tscv.H  = (tsv.b-tsv.h)/2/tscv.L;                        //H = (Z2-Z8)/(2L)

  return tscv;
}

///@brief  Calculates aspect in degrees in the manner of Horn 1981
///@return Aspect in degrees in the manner of Horn 1981
//ArcGIS doesn't use cell size for aspect calculations.
template<class T>
static inline double Terrain_Aspect(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const auto tsv = TerrainSetup(elevations,x,y,zscale);

  //See p. 18 of Horn (1981)
  double dzdx       = ( (tsv.c+2*tsv.f+tsv.i) - (tsv.a+2*tsv.d+tsv.g) ) / 8 / elevations.getCellLengthX();
  double dzdy       = ( (tsv.g+2*tsv.h+tsv.i) - (tsv.a+2*tsv.b+tsv.c) ) / 8 / elevations.getCellLengthY();
  double the_aspect = 180.0/M_PI*atan2(dzdy,-dzdx);
  if(the_aspect<0)
    return 90-the_aspect;
  else if(the_aspect>90.0)
    return 360.0-the_aspect+90.0;
  else
    return 90.0-the_aspect;
}

///@brief  Calculates the rise/run slope along the maximum gradient on a fitted surface over a 3x3 be neighbourhood in the manner of Horn 1981
///@return Rise/run slope
template<class T>
static inline double Terrain_Slope_RiseRun(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const auto tsv = TerrainSetup(elevations,x,y,zscale);

  //See p. 18 of Horn (1981)
  double dzdx = ( (tsv.c+2*tsv.f+tsv.i) - (tsv.a+2*tsv.d+tsv.g) ) / 8 / elevations.getCellLengthX();
  double dzdy = ( (tsv.g+2*tsv.h+tsv.i) - (tsv.a+2*tsv.b+tsv.c) ) / 8 / elevations.getCellLengthY();

  //The above fits are surface to a 3x3 neighbour hood. This returns the slope
  //along the direction of maximum gradient.
  return sqrt(dzdx*dzdx+dzdy*dzdy);
}

template<class T>
static inline double Terrain_Curvature(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const auto tscv = TerrainCurvatureSetup(elevations,x,y,zscale);

  return (-2*(tscv.D+tscv.E)*100);
}

template<class T>
static inline double Terrain_Planform_Curvature(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const auto p = TerrainCurvatureSetup(elevations,x,y,zscale);

  if(p.G==0 && p.H==0)
    return 0;
  else
    return (-2*(p.D*p.H*p.H+p.E*p.G*p.G-p.F*p.G*p.H)/(p.G*p.G+p.H*p.H)*100);
}

template<class T>
static inline double Terrain_Profile_Curvature(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  const auto p = TerrainCurvatureSetup(elevations,x,y,zscale);

  if(p.G==0 && p.H==0)
    return 0;
  else
    return (2*(p.D*p.G*p.G+p.E*p.H*p.H+p.F*p.G*p.H)/(p.G*p.G+p.H*p.H)*100);
}

template<class T>
static inline double Terrain_Slope_Percent(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  return Terrain_Slope_RiseRun(elevations,x,y,zscale)*100;
}

template<class T>
static inline double Terrain_Slope_Radian(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  return std::atan(Terrain_Slope_RiseRun(elevations,x,y,zscale));
}

template<class T>
static inline double Terrain_Slope_Degree(const Array2D<T> &elevations, const int x, const int y, const float zscale){
  return std::atan(Terrain_Slope_RiseRun(elevations,x,y,zscale))*180/M_PI;
}



/**
  @brief  Calculate a variety of terrain attributes
  @author Richard Barnes (rbarnes@umn.edu), Burrough (1998)

  This calculates a variety of terrain attributes according
  to the work of Burrough 1998's "Principles of Geographical
  Information Systems" (p. 190). This function calls
  d8_terrain_attrib_helper to calculate the actual attributes.
  This function may perform some post-processing (such as on
  slope), but it's purpose is essentially to just scan the grid
  and pass off the work to d8_terrain_attrib_helper().

  Possible attribute values are
  <ul>
    <li>TATTRIB_CURVATURE</li>
    <li>TATTRIB_PLANFORM_CURVATURE</li>
    <li>TATTRIB_PROFILE_CURVATURE</li>
    <li>TATTRIB_ASPECT</li>
    <li>TATTRIB_SLOPE_RISERUN</li>
    <li>TATTRIB_SLOPE_PERCENT</li>
    <li>TATTRIB_SLOPE_RADIAN</li>
    <li>TATTRIB_SLOPE_DEGREE</li>
  </ul>

  @param[in]  func         The attribute function to be used
  @param[in]  &elevations  An elevation grid
  @param[in]  zscale       Value by which to scale elevation
  @param[out] &output      A grid to hold the results

  @post \p output takes the properties and dimensions of \p elevations
*/
template<class F, class T>
static inline void TerrainProcessor(F func, const Array2D<T> &elevations, const float zscale, Array2D<float> &output){
  if(elevations.getCellLengthX()!=elevations.getCellLengthY())
    RDLOG_WARN<<"Cell X and Y dimensions are not equal!";

  output.resize(elevations);
  ProgressBar progress;

  progress.start(elevations.size());
  #pragma omp parallel for
  for(int y=0;y<elevations.height();y++){
    progress.update(y*elevations.width());
    for(int x=0;x<elevations.width();x++)
      if(elevations.isNoData(x,y))
        output(x,y) = output.noData();
      else
        output(x,y) = func(elevations,x,y,zscale);
  }
  RDLOG_TIME_USE<<"Wall-time = "<<progress.stop();
}



/**
  @brief  Calculates the slope as rise/run
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the slope using Horn 1981, as per Burrough 1998's
  "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &slopes       A slope grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_slope_riserun(
  const Array2D<T> &elevations,
  Array2D<float>   &slopes,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Slope calculation (rise/run)";
  RDLOG_CITATION<<"Horn, B.K.P., 1981. Hill shading and the reflectance map. Proceedings of the IEEE 69, 14–47. doi:10.1109/PROC.1981.11918";
  TerrainProcessor(Terrain_Slope_RiseRun<T>, elevations, zscale, slopes);
}

/**
  @brief  Calculates the slope as percentage
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the slope using Horn 1981, as per Burrough 1998's
  "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &slopes       A slope grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_slope_percentage(
  const Array2D<T> &elevations,
  Array2D<float>   &slopes,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Slope calculation (percenage)";
  RDLOG_CITATION<<"Horn, B.K.P., 1981. Hill shading and the reflectance map. Proceedings of the IEEE 69, 14–47. doi:10.1109/PROC.1981.11918";
  TerrainProcessor(Terrain_Slope_Percent<T>, elevations, zscale, slopes);
}

/**
  @brief  Calculates the slope as degrees
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the slope using Horn 1981, as per Burrough 1998's
  "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &slopes       A slope grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_slope_degrees(
  const Array2D<T> &elevations,
  Array2D<float>   &slopes,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Slope calculation (degrees)";
  RDLOG_CITATION<<"Horn, B.K.P., 1981. Hill shading and the reflectance map. Proceedings of the IEEE 69, 14–47. doi:10.1109/PROC.1981.11918";
  TerrainProcessor(Terrain_Slope_Degree<T>, elevations, zscale, slopes);
}

/**
  @brief  Calculates the slope as radians
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the slope using Horn 1981, as per Burrough 1998's
  "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &slopes       A slope grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_slope_radians(
  const Array2D<T> &elevations,
  Array2D<float>   &slopes,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Slope calculation (radians)";
  RDLOG_CITATION<<"Horn, B.K.P., 1981. Hill shading and the reflectance map. Proceedings of the IEEE 69, 14–47. doi:10.1109/PROC.1981.11918";
  TerrainProcessor(Terrain_Slope_Radian<T>, elevations, zscale, slopes);
}

/**
  @brief  Calculates the terrain aspect
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the aspect per Horn 1981, as described by Burrough 1998's
  "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &aspects      An aspect grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_aspect(
  const Array2D<T> &elevations,
  Array2D<float>   &aspects,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Aspect attribute calculation";
  RDLOG_CITATION<<"Horn, B.K.P., 1981. Hill shading and the reflectance map. Proceedings of the IEEE 69, 14–47. doi:10.1109/PROC.1981.11918";
  TerrainProcessor(Terrain_Aspect<T>, elevations, zscale, aspects);
}

/**
  @brief  Calculates the terrain curvature per Zevenbergen and Thorne 1987
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the curvature per Zevenbergen and Thorne 1987, as described by
  Burrough 1998's "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations   An elevation grid
  @param[out] &curvatures   A curvature grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_curvature(
  const Array2D<T> &elevations, 
  Array2D<float>   &curvatures, 
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Curvature attribute calculation";
  RDLOG_CITATION<<"Zevenbergen, L.W., Thorne, C.R., 1987. Quantitative analysis of land surface topography. Earth surface processes and landforms 12, 47–56.";
  TerrainProcessor(Terrain_Curvature<T>, elevations, zscale, curvatures);
}


/**
  @brief  Calculates the terrain planform curvature per Zevenbergen and Thorne 1987
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the curvature per Zevenbergen and Thorne 1987, as described by
  Burrough 1998's "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations          An elevation grid
  @param[out] &planform_curvatures A planform curvature grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_planform_curvature(
  const Array2D<T> &elevations,
  Array2D<float>   &planform_curvatures,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Planform curvature attribute calculation";
  RDLOG_CITATION<<"Zevenbergen, L.W., Thorne, C.R., 1987. Quantitative analysis of land surface topography. Earth surface processes and landforms 12, 47–56.";
  TerrainProcessor(Terrain_Planform_Curvature<T>, elevations, zscale, planform_curvatures);
}

/**
  @brief  Calculates the terrain profile curvature per Zevenbergen and Thorne 1987
  @author Richard Barnes (rbarnes@umn.edu), Horn (1981)

  Calculates the curvature per Zevenbergen and Thorne 1987, as described by
  Burrough 1998's "Principles of Geographical Information Systems" (p. 190)

  @param[in]  &elevations         An elevation grid
  @param[out] &profile_curvatures A profile curvature grid
  @param[in]   zscale       DEM is scaled by this factor prior to calculation
*/
template<class T>
void TA_profile_curvature(
  const Array2D<T> &elevations,
  Array2D<float>   &profile_curvatures,
  float zscale = 1.0f
){
  RDLOG_ALG_NAME<<"Profile curvature attribute calculation";
  RDLOG_CITATION<<"Zevenbergen, L.W., Thorne, C.R., 1987. Quantitative analysis of land surface topography. Earth surface processes and landforms 12, 47–56.";
  TerrainProcessor(Terrain_Profile_Curvature<T>, elevations, zscale, profile_curvatures);
}

}

#endif
