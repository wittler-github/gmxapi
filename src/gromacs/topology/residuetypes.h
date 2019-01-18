/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2010,2014,2018,2019, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
#ifndef GMX_TOPOLOGY_RESIDUETYPES_H
#define GMX_TOPOLOGY_RESIDUETYPES_H

#include "gromacs/utility/basedefinitions.h"
#include "gromacs/utility/classhelpers.h"

struct ResidueTypeEntry;

class ResidueType
{
    public:
        //! Default constructor.
        ResidueType();
        //! Default destructor.
        ~ResidueType();

        //! Get handle to underlying residue type data.
        ResidueTypeEntry *ResidueTypes();

        //! Get number of entries in ResidueTypes.
        int numberOfEntries() const;
        /*! \brief
         * Return true if residue \p residueName is found or false otherwise.
         * \p residueTypePointer is set to the type name, or "Other" if not found.
         *
         * \param[in] residueName Residue name to search database for.
         * \param[out] residueTypePointer Name of found type or "Other".
         * \returns true if successful.
         */
        bool nameIndexedInResidueTypes(const char *residueName, const char **residueTypePointer);
        /*! \brief
         * Add entry to ResidueTypes if unique.
         *
         * \param[in] residueName Name of new residue.
         * \param[in] residueType Type of new residue.
         */
        void addResidue(const char *residueName, const char *residueType);
        /*! \brief
         * Checks if the indicated \p residueName if of \p residueType.
         *
         * \param[in] residueName Residue that should be checked.
         * \param[in] residueType Which ResidueType the residue should have.
         * \returns If the check was successful.
         */
        bool namedResidueHasType(const char *residueName, const char *residueType);
        /*! \brief
         * Get entry index in ResidueTypes with name \p residueName.
         *
         * \param[in] residueName Name of the residue being searched.
         * \returns Value of the entry or -1.
         */
        int indexFromResidueName(const char *residueName) const;
        /*! \brief
         * Get the name of the entry in ResidueTypes with \p index.
         *
         * \param[in] index Which entry should be returned.
         * \returns The name of the entry at \p index, or nullptr.
         */
        const char *nameFromResidueIndex(int index) const;

    private:
        //! Implementation pointer.
        class Impl;

        gmx::PrivateImplPointer<Impl> impl_;
};


#endif