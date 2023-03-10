// Gmsh - Copyright (C) 1997-2013 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@geuz.org>.

#ifndef PYRAMIDALBASIS_H
#define PYRAMIDALBASIS_H

#include "fullMatrix.h"

#include "nodalBasis.h"
#include "BergotBasis.h"



class pyramidalBasis: public nodalBasis
{
 private:
  // Inverse of the Vandermonde matrix
  fullMatrix<double> VDMinv;

  // Orthogonal basis for the pyramid
  BergotBasis *bergot;

 public:
  pyramidalBasis(int tag);
  ~pyramidalBasis();

  virtual void f(double u, double v, double w, double *val) const;
  virtual void f(const fullMatrix<double> &coord, fullMatrix<double> &sf) const;
  virtual void df(double u, double v, double w, double grads[][3]) const;
  virtual void df(const fullMatrix<double> &coord, fullMatrix<double> &dfm) const;

  virtual int getNumShapeFunctions() const;

};



#endif
