// Gmsh - Copyright (C) 1997-2013 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@geuz.org>.
//
// Contributor(s):
//   Tristan Carrier Maxime Melchior

#include "GRegion.h"

class voroMetal3D{
 private:
  int counter;
 public:
  voroMetal3D();
  ~voroMetal3D();
  void execute(double);
  void execute(GRegion*,double);
  void execute(std::vector<SPoint3>&,std::vector<double>&,int,double);
  void execute(std::vector<double>&,int,double);
  void print_segment(SPoint3,SPoint3,std::ofstream&);
  void initialize_counter();
  void increase_counter();
  int get_counter();
  void print_geo_point(int,double,double,double,std::ofstream&);
  void print_geo_line(int,int,int,std::ofstream&);
  void print_geo_face(int,int,std::ofstream&);
  void print_geo_physical_face(int,int,std::ofstream&);
  void print_geo_volume(int,int,std::ofstream&);
  void print_geo_physical_volume(int,int,std::ofstream&);
  void print_geo_line_loop(int,std::vector<int>&,std::vector<int>&,std::ofstream&);
  void print_geo_face_loop(int,std::vector<int>&,std::ofstream&);
  void correspondance(double);
  bool correspondance(double,double,double,double,int&);
  void correspondance(double,double,double,double,int,bool&);
  bool equal(double,double,double);
};
