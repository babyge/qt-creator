/**************************************************************************

**

**  This  file  is  part  of  Qt  Creator

**

**  Copyright  (c)  2011  Nokia  Corporation  and/or  its  subsidiary(-ies).

**

**  Contact:  Nokia  Corporation  (info@qt.nokia.com)

**

**  No  Commercial  Usage

**

**  This  file  contains  pre-release  code  and  may  not  be  distributed.

**  You  may  use  this  file  in  accordance  with  the  terms  and  conditions

**  contained  in  the  Technology  Preview  License  Agreement  accompanying

**  this  package.

**

**  GNU  Lesser  General  Public  License  Usage

**

**  Alternatively,  this  file  may  be  used  under  the  terms  of  the  GNU  Lesser

**  General  Public  License  version  2.1  as  published  by  the  Free  Software

**  Foundation  and  appearing  in  the  file  LICENSE.LGPL  included  in  the

**  packaging  of  this  file.   Please  review  the  following  information  to

**  ensure  the  GNU  Lesser  General  Public  License  version  2.1  requirements

**  will  be  met:  http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

**

**  In  addition,  as  a  special  exception,  Nokia  gives  you  certain  additional

**  rights.   These  rights  are  described  in  the  Nokia  Qt  LGPL  Exception

**  version  1.1,  included  in  the  file  LGPL_EXCEPTION.txt  in  this  package.

**

**  If  you  have  questions  regarding  the  use  of  this  file,  please  contact

**  Nokia  at  info@qt.nokia.com.

**

**************************************************************************/

#include "qt5informationnodeinstanceserver.h"

#include <QSGItem>

#include "servernodeinstance.h"
#include "childrenchangeeventfilter.h"
#include "propertyabstractcontainer.h"
#include "propertybindingcontainer.h"
#include "propertyvaluecontainer.h"
#include "instancecontainer.h"
#include "createinstancescommand.h"
#include "changefileurlcommand.h"
#include "clearscenecommand.h"
#include "reparentinstancescommand.h"
#include "changevaluescommand.h"
#include "changebindingscommand.h"
#include "changeidscommand.h"
#include "removeinstancescommand.h"
#include "nodeinstanceclientinterface.h"
#include "removepropertiescommand.h"
#include "valueschangedcommand.h"
#include "informationchangedcommand.h"
#include "pixmapchangedcommand.h"
#include "commondefines.h"
#include "childrenchangeeventfilter.h"
#include "changestatecommand.h"
#include "childrenchangedcommand.h"
#include "completecomponentcommand.h"
#include "componentcompletedcommand.h"
#include "createscenecommand.h"

#include "dummycontextobject.h"

#include "designersupportfunctions.h"

namespace QmlDesigner {

Qt5InformationNodeInstanceServer::Qt5InformationNodeInstanceServer(NodeInstanceClientInterface *nodeInstanceClient) :
    Qt5NodeInstanceServer(nodeInstanceClient)
{
}

void Qt5InformationNodeInstanceServer::collectItemChangesAndSendChangeCommands()
{
    static bool inFunction = false;
    if (!inFunction) {
        inFunction = true;

        QSet<ServerNodeInstance> informationChangedInstanceSet;
        QVector<InstancePropertyPair> propertyChangedList;
        bool adjustSceneRect = false;

        if (sgView()) {
            foreach (QSGItem *item, allItems()) {
                if (item && hasInstanceForObject(item)) {
                    ServerNodeInstance instance = instanceForObject(item);

                    DesignerSupport::DirtyType informationsDirty = DesignerSupport::DirtyType(DesignerSupport::TransformUpdateMask
                                                                                              | DesignerSupport::Visible
                                                                                              | DesignerSupport::ZValue
                                                                                              | DesignerSupport::OpacityValue);
                    if (DesignerSupport::dirty(item, informationsDirty))
                        informationChangedInstanceSet.insert(instance);


                    if (DesignerSupport::dirty(item, DesignerSupport::ParentChanged)) {
                        m_parentChangedSet.insert(instance);
                        informationChangedInstanceSet.insert(instance);
                    }
//                    if (d->geometryChanged) {
//                        if (instance.isRootNodeInstance())
//                            declarativeView()->scene()->setSceneRect(item->boundingRect());
//                    }

                }
            }

            foreach (const InstancePropertyPair& property, changedPropertyList()) {
                const ServerNodeInstance instance = property.first;
                const QString propertyName = property.second;

                if (instance.isValid()) {
                    if (instance.isRootNodeInstance() && (propertyName == "width" || propertyName == "height"))
                        adjustSceneRect = true;

                    if (propertyName.contains("anchors"))
                        informationChangedInstanceSet.insert(instance);

                    propertyChangedList.append(property);
                }
            }

            resetAllItems();
            clearChangedPropertyList();

            if (!informationChangedInstanceSet.isEmpty())
                nodeInstanceClient()->informationChanged(createAllInformationChangedCommand(informationChangedInstanceSet.toList()));

            if (!propertyChangedList.isEmpty())
                nodeInstanceClient()->valuesChanged(createValuesChangedCommand(propertyChangedList));

            if (!m_parentChangedSet.isEmpty()) {
                sendChildrenChangedCommand(m_parentChangedSet.toList());
                m_parentChangedSet.clear();
            }

//            if (adjustSceneRect) {
//                QRectF boundingRect = rootNodeInstance().boundingRect();
//                if (boundingRect.isValid()) {
//                    declarativeView()->setSceneRect(boundingRect);
//                }
//            }

            if (!m_completedComponentList.isEmpty()) {
                nodeInstanceClient()->componentCompleted(createComponentCompletedCommand(m_completedComponentList));
                m_completedComponentList.clear();
            }

            slowDownRenderTimer();
            nodeInstanceClient()->flush();
            nodeInstanceClient()->synchronizeWithClientProcess();
        }

        inFunction = false;
    }
}

void Qt5InformationNodeInstanceServer::reparentInstances(const ReparentInstancesCommand &command)
{
    foreach (const ReparentContainer &container, command.reparentInstances()) {
        ServerNodeInstance instance = instanceForId(container.instanceId());
        if (instance.isValid()) {
            m_parentChangedSet.insert(instance);
        }
    }

    Qt5NodeInstanceServer::reparentInstances(command);
}

void Qt5InformationNodeInstanceServer::clearScene(const ClearSceneCommand &command)
{
    Qt5NodeInstanceServer::clearScene(command);

    m_parentChangedSet.clear();
    m_completedComponentList.clear();
}

void Qt5InformationNodeInstanceServer::createScene(const CreateSceneCommand &command)
{
    Qt5NodeInstanceServer::createScene(command);

    QList<ServerNodeInstance> instanceList;
    foreach (const InstanceContainer &container, command.instances()) {
        ServerNodeInstance instance = instanceForId(container.instanceId());
        if (instance.isValid()) {
            instanceList.append(instance);
        }
    }

    nodeInstanceClient()->informationChanged(createAllInformationChangedCommand(instanceList, true));
    nodeInstanceClient()->valuesChanged(createValuesChangedCommand(instanceList));
    sendChildrenChangedCommand(instanceList);
    nodeInstanceClient()->componentCompleted(createComponentCompletedCommand(instanceList));

}

void Qt5InformationNodeInstanceServer::sendChildrenChangedCommand(const QList<ServerNodeInstance> childList)
{
    QSet<ServerNodeInstance> parentSet;
    QList<ServerNodeInstance> noParentList;

    foreach (const ServerNodeInstance &child, childList) {
        if (!child.hasParent()) {
            noParentList.append(child);
        } else {
            ServerNodeInstance parent = child.parent();
            if (parent.isValid()) {
                parentSet.insert(parent);
            } else {
                noParentList.append(child);
            }
        }
    }

    foreach (const ServerNodeInstance &parent, parentSet)
        nodeInstanceClient()->childrenChanged(createChildrenChangedCommand(parent, parent.childItems()));

    if (!noParentList.isEmpty())
        nodeInstanceClient()->childrenChanged(createChildrenChangedCommand(ServerNodeInstance(), noParentList));

}

void Qt5InformationNodeInstanceServer::completeComponent(const CompleteComponentCommand &command)
{
    Qt5NodeInstanceServer::completeComponent(command);

    QList<ServerNodeInstance> instanceList;
    foreach (qint32 instanceId, command.instances()) {
        ServerNodeInstance instance = instanceForId(instanceId);
        if (instance.isValid()) {
            instanceList.append(instance);
        }
    }

    m_completedComponentList.append(instanceList);

    nodeInstanceClient()->valuesChanged(createValuesChangedCommand(instanceList));
    nodeInstanceClient()->informationChanged(createAllInformationChangedCommand(instanceList, true));
}

} // namespace QmlDesigner
