#pragma once
/**********************************************************************
 *<
        FILE: SaveLoader.H

        DESCRIPTION:  file has functions to load/store some basic objects like plots

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

/////////////////////////////////////////////////////////////////////////////////////
template <>
inline void LoadObject<CPlot3D>(CPlot3D &a, CSave &file)
{
    file.Read(&a, sizeof(a));
}

template <>
inline void StoreObject<CPlot3D>(CPlot3D &a, CSave &file)
{
    file.Write(&a, sizeof(a));
}

template <>
inline void LoadObject<CPlot2D>(CPlot2D &a, CSave &file)
{
    file.Read(&a, sizeof(a));
}

template <>
inline void StoreObject<CPlot2D>(CPlot2D &a, CSave &file)
{
    file.Write(&a, sizeof(a));
}

/////////////////////////////////////////////////////////////////////////////////////
