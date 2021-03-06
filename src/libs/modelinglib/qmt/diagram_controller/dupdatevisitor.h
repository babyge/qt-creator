/****************************************************************************
**
** Copyright (C) 2016 Jochen Becher
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#ifndef QMT_DUPDATEVISITOR_H
#define QMT_DUPDATEVISITOR_H

#include "qmt/model/mconstvisitor.h"
#include "qmt/infrastructure/qmt_global.h"

namespace qmt {

class DElement;

class QMT_EXPORT DUpdateVisitor : public MConstVisitor
{
public:
    DUpdateVisitor(DElement *target, const MDiagram *diagram, bool checkNeedsUpdate = false);

    bool isUpdateNeeded() const { return m_isUpdateNeeded; }
    void setCheckNeedsUpdate(bool checkNeedsUpdate);

    void visitMElement(const MElement *element) override;
    void visitMObject(const MObject *object) override;
    void visitMPackage(const MPackage *package) override;
    void visitMClass(const MClass *klass) override;
    void visitMComponent(const MComponent *component) override;
    void visitMDiagram(const MDiagram *diagram) override;
    void visitMCanvasDiagram(const MCanvasDiagram *diagram) override;
    void visitMItem(const MItem *item) override;
    void visitMRelation(const MRelation *relation) override;
    void visitMDependency(const MDependency *dependency) override;
    void visitMInheritance(const MInheritance *inheritance) override;
    void visitMAssociation(const MAssociation *association) override;

private:
    bool isUpdating(bool valueChanged);

    DElement *m_target;
    const MDiagram *m_diagram;
    bool m_checkNeedsUpdate;
    bool m_isUpdateNeeded;
};

} // namespace qmt

#endif // QMT_DUPDATEVISITOR_H
