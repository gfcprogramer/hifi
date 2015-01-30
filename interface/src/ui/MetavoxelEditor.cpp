//
//  MetavoxelEditor.cpp
//  interface/src/ui
//
//  Created by Andrzej Kapolka on 1/21/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <limits>

// include this before QOpenGLFramebufferObject, which includes an earlier version of OpenGL
#include "InterfaceConfig.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaProperty>
#include <QOpenGLFramebufferObject>
#include <QPushButton>
#include <QRunnable>
#include <QScrollArea>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>

#include <AttributeRegistry.h>
#include <GeometryCache.h>
#include <MetavoxelMessages.h>
#include <MetavoxelUtil.h>
#include <PathUtils.h>

#include "Application.h"
#include "MainWindow.h"
#include "MetavoxelEditor.h"

using namespace std;

enum GridPlane {
    GRID_PLANE_XY, GRID_PLANE_XZ, GRID_PLANE_YZ
};

const glm::vec2 INVALID_VECTOR(FLT_MAX, FLT_MAX);

MetavoxelEditor::MetavoxelEditor(QWidget* parent) :
    QWidget(parent, Qt::Tool) {
    
    setWindowTitle("Metavoxel Editor");
    setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout* topLayout = new QVBoxLayout();
    setLayout(topLayout);
    
    QGroupBox* attributeGroup = new QGroupBox();
    attributeGroup->setTitle("Attributes");
    topLayout->addWidget(attributeGroup);
    
    QVBoxLayout* attributeLayout = new QVBoxLayout();
    attributeGroup->setLayout(attributeLayout);
    
    attributeLayout->addWidget(_attributes = new QListWidget());
    connect(_attributes, SIGNAL(itemSelectionChanged()), SLOT(selectedAttributeChanged()));

    QHBoxLayout* attributeButtonLayout = new QHBoxLayout();
    attributeLayout->addLayout(attributeButtonLayout);

    QPushButton* newAttribute = new QPushButton("New...");
    attributeButtonLayout->addWidget(newAttribute, 1);
    connect(newAttribute, SIGNAL(clicked()), SLOT(createNewAttribute()));

    attributeButtonLayout->addWidget(_deleteAttribute = new QPushButton("Delete"), 1);
    _deleteAttribute->setEnabled(false);
    connect(_deleteAttribute, SIGNAL(clicked()), SLOT(deleteSelectedAttribute()));

    attributeButtonLayout->addWidget(_showAll = new QCheckBox("Show All"));
    connect(_showAll, SIGNAL(clicked()), SLOT(updateAttributes()));

    QFormLayout* formLayout = new QFormLayout();
    topLayout->addLayout(formLayout);
    
    formLayout->addRow("Grid Plane:", _gridPlane = new QComboBox());
    _gridPlane->addItem("X/Y");
    _gridPlane->addItem("X/Z");
    _gridPlane->addItem("Y/Z");
    _gridPlane->setCurrentIndex(GRID_PLANE_XZ);
    connect(_gridPlane, SIGNAL(currentIndexChanged(int)), SLOT(centerGridPosition()));
    
    formLayout->addRow("Grid Spacing:", _gridSpacing = new QDoubleSpinBox());
    _gridSpacing->setMinimum(-FLT_MAX);
    _gridSpacing->setMaximum(FLT_MAX);
    _gridSpacing->setPrefix("2^");
    _gridSpacing->setValue(0.0);
    connect(_gridSpacing, SIGNAL(valueChanged(double)), SLOT(alignGridPosition()));

    formLayout->addRow("Grid Position:", _gridPosition = new QDoubleSpinBox());
    _gridPosition->setMinimum(-FLT_MAX);
    _gridPosition->setMaximum(FLT_MAX);
    alignGridPosition();
    centerGridPosition();
    
    formLayout->addRow("Tool:", _toolBox = new QComboBox());
    connect(_toolBox, SIGNAL(currentIndexChanged(int)), SLOT(updateTool()));
    
    _value = new QGroupBox();
    _value->setTitle("Value");
    topLayout->addWidget(_value);
    
    QVBoxLayout* valueLayout = new QVBoxLayout();
    _value->setLayout(valueLayout);

    valueLayout->addWidget(_valueArea = new QScrollArea());
    _valueArea->setMinimumHeight(200);
    _valueArea->setWidgetResizable(true);

    addTool(new BoxSetTool(this));
    addTool(new GlobalSetTool(this));
    addTool(new InsertSpannerTool(this));
    addTool(new RemoveSpannerTool(this));
    addTool(new ClearSpannersTool(this));
    addTool(new ImportHeightfieldTool(this));
    addTool(new HeightfieldHeightBrushTool(this));
    addTool(new HeightfieldMaterialBrushTool(this));
    addTool(new HeightfieldSculptBrushTool(this));
    addTool(new HeightfieldFillBrushTool(this));    
    addTool(new HeightfieldMaterialBoxTool(this));
    addTool(new HeightfieldMaterialSpannerTool(this));
    
    updateAttributes();
    
    connect(Application::getInstance(), SIGNAL(simulating(float)), SLOT(simulate(float)));
    connect(Application::getInstance(), SIGNAL(renderingInWorldInterface()), SLOT(render()));
    connect(Application::getInstance()->getMetavoxels(), &MetavoxelSystem::rendering,
        this, &MetavoxelEditor::renderPreview);
    
    DependencyManager::get<GLCanvas>()->installEventFilter(this);
    
    show();
    
    if (_gridProgram.isLinked()) {
        return;
    }
        
    _gridProgram.addShaderFromSourceFile(QGLShader::Vertex, PathUtils::resourcesPath() + "shaders/grid.vert");
    _gridProgram.addShaderFromSourceFile(QGLShader::Fragment, PathUtils::resourcesPath() + "shaders/grid.frag");
    _gridProgram.link();
}

QString MetavoxelEditor::getSelectedAttribute() const {
    QList<QListWidgetItem*> selectedItems = _attributes->selectedItems();
    return selectedItems.isEmpty() ? QString() : selectedItems.first()->text();
}

double MetavoxelEditor::getGridSpacing() const {
    return pow(2.0, _gridSpacing->value());
}

double MetavoxelEditor::getGridPosition() const {
    return _gridPosition->value();
}

glm::quat MetavoxelEditor::getGridRotation() const {
    // for simplicity, we handle the other two planes by rotating them onto X/Y and performing computation there
    switch (_gridPlane->currentIndex()) {
        case GRID_PLANE_XY:
            return glm::quat();
            
        case GRID_PLANE_XZ:
            return glm::angleAxis(-PI_OVER_TWO, glm::vec3(1.0f, 0.0f, 0.0f));
            
        case GRID_PLANE_YZ:
        default:
            return glm::angleAxis(PI_OVER_TWO, glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

QVariant MetavoxelEditor::getValue() const {
    QWidget* editor = _valueArea->widget();
    return editor ? editor->metaObject()->userProperty().read(editor) : QVariant();
}

bool MetavoxelEditor::eventFilter(QObject* watched, QEvent* event) {
    // pass along to the active tool
    MetavoxelTool* tool = getActiveTool();
    return tool && tool->eventFilter(watched, event);
}

void MetavoxelEditor::selectedAttributeChanged() {
    _toolBox->clear();
    
    QString selected = getSelectedAttribute();
    if (selected.isNull()) {
        _deleteAttribute->setEnabled(false);
        _toolBox->setEnabled(false); 
        _value->setVisible(false);
        return;
    }
    _deleteAttribute->setEnabled(true);
    _toolBox->setEnabled(true);
    
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(selected);
    foreach (MetavoxelTool* tool, _tools) {
        if (tool->appliesTo(attribute) && (tool->isUserFacing() || _showAll->isChecked())) {
            _toolBox->addItem(tool->objectName(), QVariant::fromValue(tool));
        }
    }
    _value->setVisible(true);
    
    if (_valueArea->widget()) {
        delete _valueArea->widget();
    }
    QWidget* editor = attribute->createEditor();
    if (editor) {
        editor->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        _valueArea->setWidget(editor);
    }
    updateTool();
}

void MetavoxelEditor::createNewAttribute() {
    QDialog dialog(this);
    dialog.setWindowTitle("New Attribute");
    
    QVBoxLayout layout;
    dialog.setLayout(&layout);
    
    QFormLayout form;
    layout.addLayout(&form);
    
    QLineEdit name;
    form.addRow("Name:", &name);
    
    SharedObjectEditor editor(&Attribute::staticMetaObject, false);
    editor.setObject(new FloatAttribute());
    layout.addWidget(&editor);
    
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialog.connect(&buttons, SIGNAL(accepted()), SLOT(accept()));
    dialog.connect(&buttons, SIGNAL(rejected()), SLOT(reject()));
    
    layout.addWidget(&buttons);
    
    if (!dialog.exec()) {
        return;
    }
    QString nameText = name.text().trimmed();
    SharedObjectPointer attribute = editor.getObject();
    attribute->setObjectName(nameText);
    AttributeRegistry::getInstance()->registerAttribute(attribute.staticCast<Attribute>());
    
    updateAttributes(nameText);
}

void MetavoxelEditor::deleteSelectedAttribute() {
    AttributeRegistry::getInstance()->deregisterAttribute(getSelectedAttribute());
    _attributes->selectionModel()->clear();
    updateAttributes();
}

void MetavoxelEditor::centerGridPosition() {
    const float CENTER_OFFSET = 0.625f;
    float eyePosition = (glm::inverse(getGridRotation()) * Application::getInstance()->getCamera()->getPosition()).z -
        Application::getInstance()->getAvatar()->getScale() * CENTER_OFFSET;
    double step = getGridSpacing();
    _gridPosition->setValue(step * floor(eyePosition / step));
}

void MetavoxelEditor::alignGridPosition() {
    // make sure our grid position matches our grid spacing
    double step = getGridSpacing();
    _gridPosition->setSingleStep(step);
    _gridPosition->setValue(step * floor(_gridPosition->value() / step));
}

void MetavoxelEditor::updateAttributes(const QString& select) {
    // remember the selection in order to preserve it
    QString selected = select.isNull() ? getSelectedAttribute() : select;
    _attributes->clear();
    
    // sort the names for consistent ordering
    QList<QString> names;
    if (_showAll->isChecked()) {
        names = AttributeRegistry::getInstance()->getAttributes().keys();
    
    } else {
        foreach (const AttributePointer& attribute, AttributeRegistry::getInstance()->getAttributes()) {
            if (attribute->isUserFacing()) {
                names.append(attribute->getName());
            }
        }
    }
    qSort(names);
    
    foreach (const QString& name, names) {
        QListWidgetItem* item = new QListWidgetItem(name);
        _attributes->addItem(item);
        if (name == selected || selected.isNull()) {
            item->setSelected(true);
            selected = name;
        }
    }
}

void MetavoxelEditor::updateTool() {
    MetavoxelTool* active = getActiveTool();
    foreach (MetavoxelTool* tool, _tools) {
        tool->setVisible(tool == active);
    }
    _value->setVisible(active && active->getUsesValue());
}

void MetavoxelEditor::simulate(float deltaTime) {
    MetavoxelTool* tool = getActiveTool();
    if (tool) {
        tool->simulate(deltaTime);
    }
}

const float GRID_BRIGHTNESS = 0.5f;

void MetavoxelEditor::render() {
    glDisable(GL_LIGHTING);
    
    MetavoxelTool* tool = getActiveTool();
    if (tool) {
        tool->render();
        if (!tool->getUsesGrid()) {
            return;
        }
    }
    
    glDepthMask(GL_FALSE);
    
    glPushMatrix();
    
    glm::quat rotation = getGridRotation();
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);
    
    glLineWidth(1.0f);
    
    // center the grid around the camera position on the plane
    glm::vec3 rotated = glm::inverse(rotation) * Application::getInstance()->getCamera()->getPosition();
    float spacing = getGridSpacing();
    const int GRID_DIVISIONS = 300;
    glTranslatef(spacing * (floorf(rotated.x / spacing) - GRID_DIVISIONS / 2),
        spacing * (floorf(rotated.y / spacing) - GRID_DIVISIONS / 2), _gridPosition->value());
    
    float scale = GRID_DIVISIONS * spacing;
    glScalef(scale, scale, scale);
    
    _gridProgram.bind();
    
    DependencyManager::get<GeometryCache>()->renderGrid(GRID_DIVISIONS, GRID_DIVISIONS, glm::vec4(GRID_BRIGHTNESS, GRID_BRIGHTNESS, GRID_BRIGHTNESS, 1.0f));
    
    _gridProgram.release();
    
    glPopMatrix();
    
    glEnable(GL_LIGHTING);
    glDepthMask(GL_TRUE);
}

void MetavoxelEditor::renderPreview() {
    MetavoxelTool* tool = getActiveTool();
    if (tool) {
        tool->renderPreview();
    }
}

void MetavoxelEditor::addTool(MetavoxelTool* tool) {
    _tools.append(tool);
    layout()->addWidget(tool);
}

MetavoxelTool* MetavoxelEditor::getActiveTool() const {
    int index = _toolBox->currentIndex();
    return (index == -1) ? NULL : static_cast<MetavoxelTool*>(_toolBox->itemData(index).value<QObject*>());
}

ProgramObject MetavoxelEditor::_gridProgram;

MetavoxelTool::MetavoxelTool(MetavoxelEditor* editor, const QString& name, bool usesValue, bool userFacing, bool usesGrid) :
    _editor(editor),
    _usesValue(usesValue),
    _userFacing(userFacing),
    _usesGrid(usesGrid) {
    
    QVBoxLayout* layout = new QVBoxLayout();
    setLayout(layout);
    
    setObjectName(name);
    setVisible(false);
}

bool MetavoxelTool::appliesTo(const AttributePointer& attribute) const {
    // shared object sets are a special case
    return !attribute->inherits("SharedObjectSetAttribute");
}

void MetavoxelTool::simulate(float deltaTime) {
    // nothing by default
}

void MetavoxelTool::render() {
    // nothing by default
}

void MetavoxelTool::renderPreview() {
    // nothing by default
}

BoxTool::BoxTool(MetavoxelEditor* editor, const QString& name, bool usesValue, bool userFacing) :
    MetavoxelTool(editor, name, usesValue, userFacing) {
    
    resetState();
}

void BoxTool::render() {
    if (Application::getInstance()->isMouseHidden()) {
        resetState();
        return;
    }
    QString selected = _editor->getSelectedAttribute();
    if (selected.isNull()) {
        resetState();
        return;
    }
    glDepthMask(GL_FALSE);
    
    glPushMatrix();
    
    glm::quat rotation = _editor->getGridRotation();
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);
    
    glm::quat inverseRotation = glm::inverse(rotation);
    glm::vec3 rayOrigin = inverseRotation * Application::getInstance()->getMouseRayOrigin();
    glm::vec3 rayDirection = inverseRotation * Application::getInstance()->getMouseRayDirection();
    float spacing = shouldSnapToGrid() ? _editor->getGridSpacing() : 0.0f;
    float position = _editor->getGridPosition();
    if (_state == RAISING_STATE) {
        // find the plane at the mouse position, orthogonal to the plane, facing the eye position
        glLineWidth(4.0f);  
        glm::vec3 eyePosition = inverseRotation * Application::getInstance()->getViewFrustum()->getOffsetPosition();
        glm::vec3 mousePoint = glm::vec3(_mousePosition, position);
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), eyePosition - mousePoint);
        glm::vec3 normal = glm::cross(right, glm::vec3(0.0f, 0.0f, 1.0f));
        float divisor = glm::dot(normal, rayDirection);
        if (fabs(divisor) > EPSILON) {
            float distance = (glm::dot(normal, mousePoint) - glm::dot(normal, rayOrigin)) / divisor;
            float projection = rayOrigin.z + distance * rayDirection.z;
            _height = (spacing == 0.0f ? projection : spacing * roundf(projection / spacing)) - position;
        }
    } else if (fabs(rayDirection.z) > EPSILON) {
        // find the intersection of the rotated mouse ray with the plane
        float distance = (position - rayOrigin.z) / rayDirection.z;
        _mousePosition = glm::vec2(rayOrigin + rayDirection * distance);
        glm::vec2 snappedPosition = (spacing == 0.0f) ? _mousePosition : spacing * glm::floor(_mousePosition / spacing);
        
        if (_state == HOVERING_STATE) {
            _startPosition = _endPosition = snappedPosition;
            glLineWidth(2.0f);
            
        } else if (_state == DRAGGING_STATE) {
            _endPosition = snappedPosition;
            glLineWidth(4.0f);
        }
    } else {
        // cancel any operation in progress
        resetState();
    }
    
    if (_startPosition != INVALID_VECTOR) {   
        glm::vec2 minimum = glm::min(_startPosition, _endPosition);
        glm::vec2 maximum = glm::max(_startPosition, _endPosition);
    
        glPushMatrix();
        glTranslatef(minimum.x, minimum.y, position);
        glScalef(maximum.x + spacing - minimum.x, maximum.y + spacing - minimum.y, _height);
    
        glTranslatef(0.5f, 0.5f, 0.5f);
        if (_state != HOVERING_STATE) {
            const float BOX_ALPHA = 0.25f;
            QColor color = getColor();
            glm::vec4 cubeColor;
            if (color.isValid()) {
                cubeColor = glm::vec4(color.redF(), color.greenF(), color.blueF(), BOX_ALPHA);
            } else {
                cubeColor = glm::vec4(GRID_BRIGHTNESS, GRID_BRIGHTNESS, GRID_BRIGHTNESS, BOX_ALPHA);
            }
            glEnable(GL_CULL_FACE);
            DependencyManager::get<GeometryCache>()->renderSolidCube(1.0f, cubeColor);
            glDisable(GL_CULL_FACE);
        }
        DependencyManager::get<GeometryCache>()->renderWireCube(1.0f, glm::vec4(GRID_BRIGHTNESS, GRID_BRIGHTNESS, GRID_BRIGHTNESS, 1.0f));
        glPopMatrix();   
    }
    
    glPopMatrix();
}

bool BoxTool::eventFilter(QObject* watched, QEvent* event) {
    switch (_state) {
        case HOVERING_STATE:
            if (event->type() == QEvent::MouseButtonPress && _startPosition != INVALID_VECTOR) {
                _state = DRAGGING_STATE;
                return true;
            }
            break;
            
        case DRAGGING_STATE:
            if (event->type() == QEvent::MouseButtonRelease) {
                _state = RAISING_STATE;
                return true;
            }
            break;
            
        case RAISING_STATE:
            if (event->type() == QEvent::MouseButtonPress) {
                if (_height != 0) {
                    // find the start and end corners in X/Y
                    float base = _editor->getGridPosition();
                    float top = base + _height;
                    glm::quat rotation = _editor->getGridRotation();
                    glm::vec3 start = rotation * glm::vec3(glm::min(_startPosition, _endPosition), glm::min(base, top));
                    float spacing = shouldSnapToGrid() ? _editor->getGridSpacing() : 0.0f;
                    glm::vec3 end = rotation * glm::vec3(glm::max(_startPosition, _endPosition) +
                        glm::vec2(spacing, spacing), glm::max(base, top));
                    
                    // find the minimum and maximum extents after rotation
                    applyValue(glm::min(start, end), glm::max(start, end));
                }
                resetState();
                return true;
            }
            break;
    }
    return false;
}

bool BoxTool::shouldSnapToGrid() {
    return true;
}

void BoxTool::resetState() {
    _state = HOVERING_STATE;
    _startPosition = INVALID_VECTOR;
    _height = 0.0f;
}

BoxSetTool::BoxSetTool(MetavoxelEditor* editor) :
    BoxTool(editor, "Set Value (Box)", true, false) {
}

QColor BoxSetTool::getColor() {
    return _editor->getValue().value<QColor>();
}

void BoxSetTool::applyValue(const glm::vec3& minimum, const glm::vec3& maximum) {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (!attribute) {
        return;
    }
    OwnedAttributeValue value(attribute, attribute->createFromVariant(_editor->getValue()));
    MetavoxelEditMessage message = { QVariant::fromValue(BoxSetEdit(Box(minimum, maximum),
        _editor->getGridSpacing(), value)) };
    Application::getInstance()->getMetavoxels()->applyEdit(message);
}

GlobalSetTool::GlobalSetTool(MetavoxelEditor* editor) :
    MetavoxelTool(editor, "Set Value (Global)", true, false) {
    
    QPushButton* button = new QPushButton("Apply");
    layout()->addWidget(button);
    connect(button, SIGNAL(clicked()), SLOT(apply()));
}

void GlobalSetTool::apply() {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (!attribute) {
        return;
    }
    OwnedAttributeValue value(attribute, attribute->createFromVariant(_editor->getValue()));
    MetavoxelEditMessage message = { QVariant::fromValue(GlobalSetEdit(value)) };
    Application::getInstance()->getMetavoxels()->applyEdit(message);
}

PlaceSpannerTool::PlaceSpannerTool(MetavoxelEditor* editor, const QString& name, const QString& placeText, bool usesValue) :
    MetavoxelTool(editor, name, usesValue) {
    
    QWidget* widget = new QWidget(this);
    layout()->addWidget(widget);
    QHBoxLayout* box = new QHBoxLayout();
    widget->setLayout(box);
    box->setContentsMargins(QMargins());
    box->addStretch(1);
    box->addWidget(_followMouse = new QCheckBox("Follow Mouse"));
    _followMouse->setChecked(true);
    box->addStretch(1);
    
    if (!placeText.isEmpty()) {
        QPushButton* button = new QPushButton(placeText);
        layout()->addWidget(button);
        connect(button, SIGNAL(clicked()), SLOT(place()));
    }
}

void PlaceSpannerTool::simulate(float deltaTime) {
    Spanner* spanner = static_cast<Spanner*>(getSpanner().data());
    Transformable* transformable = qobject_cast<Transformable*>(spanner);
    if (transformable && _followMouse->isChecked() && !Application::getInstance()->isMouseHidden()) {
        // find the intersection of the mouse ray with the grid and place the transformable there
        glm::quat rotation = _editor->getGridRotation();
        glm::quat inverseRotation = glm::inverse(rotation);
        glm::vec3 rayOrigin = inverseRotation * Application::getInstance()->getMouseRayOrigin();
        glm::vec3 rayDirection = inverseRotation * Application::getInstance()->getMouseRayDirection();
        float position = _editor->getGridPosition();
        float distance = (position - rayOrigin.z) / rayDirection.z;
        
        transformable->setTranslation(rotation * glm::vec3(glm::vec2(rayOrigin + rayDirection * distance), position));
    }
    spanner->getRenderer()->simulate(deltaTime);
}

void PlaceSpannerTool::renderPreview() {
    Spanner* spanner = static_cast<Spanner*>(getSpanner().data());
    spanner->getRenderer()->render(Application::getInstance()->getMetavoxels()->getLOD());
}

bool PlaceSpannerTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

bool PlaceSpannerTool::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        place();
        return true;
    }
    return false;
}

SharedObjectPointer PlaceSpannerTool::getSpanner() {
    return _editor->getValue().value<SharedObjectPointer>();
}

QColor PlaceSpannerTool::getColor() {
    return Qt::white;
}

void PlaceSpannerTool::place() {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (attribute) {
        applyEdit(attribute, getSpanner()->clone());
    }
}

InsertSpannerTool::InsertSpannerTool(MetavoxelEditor* editor) :
    PlaceSpannerTool(editor, "Insert Spanner", "Insert") {
}

void InsertSpannerTool::applyEdit(const AttributePointer& attribute, const SharedObjectPointer& spanner) {
    MetavoxelEditMessage message = { QVariant::fromValue(InsertSpannerEdit(attribute, spanner)) };
    Application::getInstance()->getMetavoxels()->applyEdit(message, true);
}

RemoveSpannerTool::RemoveSpannerTool(MetavoxelEditor* editor) :
    MetavoxelTool(editor, "Remove Spanner", false, true, false) {
}

bool RemoveSpannerTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

bool RemoveSpannerTool::eventFilter(QObject* watched, QEvent* event) {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (!attribute) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        float distance;
        SharedObjectPointer spanner = Application::getInstance()->getMetavoxels()->findFirstRaySpannerIntersection(
            Application::getInstance()->getMouseRayOrigin(), Application::getInstance()->getMouseRayDirection(),
            attribute, distance);
        if (spanner) {
            MetavoxelEditMessage message = { QVariant::fromValue(RemoveSpannerEdit(attribute, spanner->getRemoteID())) };
            Application::getInstance()->getMetavoxels()->applyEdit(message);
        }
        return true;
    }
    return false;
}

ClearSpannersTool::ClearSpannersTool(MetavoxelEditor* editor) :
    MetavoxelTool(editor, "Clear Spanners", false, true, false) {
    
    QPushButton* button = new QPushButton("Clear");
    layout()->addWidget(button);
    connect(button, SIGNAL(clicked()), SLOT(clear()));
}

bool ClearSpannersTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

void ClearSpannersTool::clear() {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (!attribute) {
        return;
    }
    MetavoxelEditMessage message = { QVariant::fromValue(ClearSpannersEdit(attribute)) };
    Application::getInstance()->getMetavoxels()->applyEdit(message);
}

HeightfieldTool::HeightfieldTool(MetavoxelEditor* editor, const QString& name) :
    MetavoxelTool(editor, name, false, true, false) {
    
    QWidget* widget = new QWidget();
    widget->setLayout(_form = new QFormLayout());
    layout()->addWidget(widget);
    
    _form->addRow("Translation:", _translation = new Vec3Editor(widget));
    _form->addRow("Spacing:", _spacing = new QDoubleSpinBox());
    _spacing->setMaximum(FLT_MAX);
    _spacing->setDecimals(3);
    _spacing->setSingleStep(0.001);
    _spacing->setValue(1.0);
    
    QPushButton* applyButton = new QPushButton("Apply");
    layout()->addWidget(applyButton);
    connect(applyButton, &QAbstractButton::clicked, this, &HeightfieldTool::apply);
}

bool HeightfieldTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

ImportHeightfieldTool::ImportHeightfieldTool(MetavoxelEditor* editor) :
    HeightfieldTool(editor, "Import Heightfield"),
    _spanner(new Heightfield()) {
    
    _form->addRow("Height Scale:", _heightScale = new QDoubleSpinBox());
    _heightScale->setMaximum(FLT_MAX);
    _heightScale->setSingleStep(0.01);
    _heightScale->setValue(16.0);
    connect(_heightScale, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &ImportHeightfieldTool::updateSpanner);
        
    _form->addRow("Height Offset:", _heightOffset = new QDoubleSpinBox());
    _heightOffset->setMinimum(-FLT_MAX);
    _heightOffset->setMaximum(FLT_MAX);
    _heightOffset->setSingleStep(0.01);
    connect(_heightOffset, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &ImportHeightfieldTool::updateSpanner);
    
    _form->addRow("Height:", _height = new HeightfieldHeightEditor(this));
    connect(_height, &HeightfieldHeightEditor::heightChanged, this, &ImportHeightfieldTool::updateSpanner);
    
    _form->addRow("Color:", _color = new HeightfieldColorEditor(this));
    connect(_color, &HeightfieldColorEditor::colorChanged, this, &ImportHeightfieldTool::updateSpanner);
    
    connect(_translation, &Vec3Editor::valueChanged, this, &ImportHeightfieldTool::updateSpanner);
    connect(_spacing, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
        &ImportHeightfieldTool::updateSpanner);
}

void ImportHeightfieldTool::simulate(float deltaTime) {
    static_cast<Heightfield*>(_spanner.data())->getRenderer()->simulate(deltaTime);
}

void ImportHeightfieldTool::renderPreview() {
    static_cast<Heightfield*>(_spanner.data())->getRenderer()->render(Application::getInstance()->getMetavoxels()->getLOD());
}

void ImportHeightfieldTool::apply() {
    AttributePointer attribute = AttributeRegistry::getInstance()->getAttribute(_editor->getSelectedAttribute());
    if (!(_height->getHeight() && attribute)) {
        return;
    }
    MetavoxelEditMessage message = { QVariant::fromValue(InsertSpannerEdit(attribute, _spanner->clone())) };
    Application::getInstance()->getMetavoxels()->applyEdit(message, true);       
}

void ImportHeightfieldTool::updateSpanner() {
    Heightfield* heightfield = static_cast<Heightfield*>(_spanner.data());
    heightfield->setHeight(_height->getHeight());
    heightfield->setColor(_color->getColor());
    
    float scale = 1.0f;
    float aspectZ = 1.0f;
    if (_height->getHeight()) {
        int width = _height->getHeight()->getWidth();
        int innerWidth = width - HeightfieldHeight::HEIGHT_EXTENSION;
        int innerHeight = _height->getHeight()->getContents().size() / width - HeightfieldHeight::HEIGHT_EXTENSION;
        scale = innerWidth * _spacing->value();
        aspectZ = (float)innerHeight / innerWidth;
    }
    heightfield->setScale(scale);
    heightfield->setAspectY(_heightScale->value() / scale);
    heightfield->setAspectZ(aspectZ);
    heightfield->setTranslation(_translation->getValue() + glm::vec3(0.0f, _heightOffset->value(), 0.0f));
}

HeightfieldBrushTool::HeightfieldBrushTool(MetavoxelEditor* editor, const QString& name) :
    MetavoxelTool(editor, name, false, true, false),
    _positionValid(false) {
    
    QWidget* widget = new QWidget();
    widget->setLayout(_form = new QFormLayout());
    layout()->addWidget(widget);
    
    _form->addRow("Radius:", _radius = new QDoubleSpinBox());
    _radius->setSingleStep(0.01);
    _radius->setMaximum(FLT_MAX);
    _radius->setValue(5.0);
    
    _form->addRow("Granularity:", _granularity = new QDoubleSpinBox());
    _granularity->setMinimum(-FLT_MAX);
    _granularity->setMaximum(FLT_MAX);
    _granularity->setPrefix("2^");
    _granularity->setValue(8.0);
}

bool HeightfieldBrushTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

void HeightfieldBrushTool::render() {
    if (Application::getInstance()->isMouseHidden()) {
        return;
    }
    
    // find the intersection with the heightfield
    glm::vec3 origin = Application::getInstance()->getMouseRayOrigin();
    glm::vec3 direction = Application::getInstance()->getMouseRayDirection();
    
    float distance;
    if (!Application::getInstance()->getMetavoxels()->findFirstRayHeightfieldIntersection(origin, direction, distance)) {
        _positionValid = false;
        return;
    }
    _positionValid = true;
    Application::getInstance()->getMetavoxels()->renderHeightfieldCursor(
        _position = origin + distance * direction, _radius->value());
}

bool HeightfieldBrushTool::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        float angle = static_cast<QWheelEvent*>(event)->angleDelta().y();
        const float ANGLE_SCALE = 1.0f / 1000.0f;
        _radius->setValue(_radius->value() * pow(2.0f, angle * ANGLE_SCALE));
        return true;
    
    } else if (event->type() == QEvent::MouseButtonPress && _positionValid) {
        MetavoxelEditMessage message = { createEdit(static_cast<QMouseEvent*>(event)->button() == Qt::RightButton) };
        Application::getInstance()->getMetavoxels()->applyEdit(message, true);
        return true;
    }
    return false;
}

HeightfieldHeightBrushTool::HeightfieldHeightBrushTool(MetavoxelEditor* editor) :
    HeightfieldBrushTool(editor, "Height Brush") {
    
    _form->addRow("Height:", _height = new QDoubleSpinBox());
    _height->setMinimum(-FLT_MAX);
    _height->setMaximum(FLT_MAX);
    _height->setValue(1.0);
    
    _form->addRow("Mode:", _mode = new QComboBox());
    _mode->addItem("Raise/Lower");
    _mode->addItem("Set");
    _mode->addItem("Erase");
}

QVariant HeightfieldHeightBrushTool::createEdit(bool alternate) {
    const int SET_MODE_INDEX = 1;
    const int ERASE_MODE_INDEX = 2;
    return QVariant::fromValue(PaintHeightfieldHeightEdit(_position, _radius->value(),
        alternate ? -_height->value() : _height->value(), _mode->currentIndex() == SET_MODE_INDEX,
            _mode->currentIndex() == ERASE_MODE_INDEX, pow(2.0f, _granularity->value())));
}

MaterialControl::MaterialControl(QWidget* widget, QFormLayout* form, bool clearable) :
    QObject(widget) {
    
    QHBoxLayout* colorLayout = new QHBoxLayout();
    form->addRow(colorLayout);
    colorLayout->addWidget(new QLabel("Color:"));
    colorLayout->addWidget(_color = new QColorEditor(widget), 1);
    connect(_color, &QColorEditor::colorChanged, this, &MaterialControl::clearTexture);
    if (clearable) {
        QPushButton* eraseButton = new QPushButton("Erase");
        colorLayout->addWidget(eraseButton);
        connect(eraseButton, &QPushButton::clicked, this, &MaterialControl::clearColor);
    }
    
    form->addRow(_materialEditor = new SharedObjectEditor(&MaterialObject::staticMetaObject, false));
    connect(_materialEditor, &SharedObjectEditor::objectChanged, this, &MaterialControl::updateTexture);
}

SharedObjectPointer MaterialControl::getMaterial() {
    SharedObjectPointer material = _materialEditor->getObject();
    if (static_cast<MaterialObject*>(material.data())->getDiffuse().isValid()) {
        material = material->clone();
        
    } else {
        material = SharedObjectPointer();
    }
    return material;
}

void MaterialControl::clearColor() {
    _color->setColor(QColor(0, 0, 0, 0));
    clearTexture();
}

void MaterialControl::clearTexture() {
    _materialEditor->setObject(new MaterialObject());
}

void MaterialControl::updateTexture() {
    if (_texture) {
        _texture->disconnect(this);
    }
    MaterialObject* material = static_cast<MaterialObject*>(_materialEditor->getObject().data());
    if (!material->getDiffuse().isValid()) {
        _texture.clear();
        return;
    }
    _texture = DependencyManager::get<TextureCache>()->getTexture(material->getDiffuse(), SPLAT_TEXTURE);
    if (_texture) {
        if (_texture->isLoaded()) {
            textureLoaded();
        } else {
            connect(_texture.data(), &Resource::loaded, this, &MaterialControl::textureLoaded);
        }
    }
}

void MaterialControl::textureLoaded() {
    _color->setColor(_texture->getAverageColor());
}

HeightfieldMaterialBrushTool::HeightfieldMaterialBrushTool(MetavoxelEditor* editor) :
    HeightfieldBrushTool(editor, "Material Brush"),
    _materialControl(new MaterialControl(this, _form)) {
}

QVariant HeightfieldMaterialBrushTool::createEdit(bool alternate) {
    Sphere* sphere = new Sphere();
    sphere->setTranslation(_position);
    sphere->setScale(_radius->value());
    if (alternate) {
        return QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(sphere),
            SharedObjectPointer(), QColor(0, 0, 0, 0), true, false, pow(2.0f, _granularity->value())));
    } else {
        return QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(sphere),
            _materialControl->getMaterial(), _materialControl->getColor(),
            true, false, pow(2.0f, _granularity->value())));
    }
}

HeightfieldSculptBrushTool::HeightfieldSculptBrushTool(MetavoxelEditor* editor) :
    HeightfieldBrushTool(editor, "Sculpt Brush"),
    _materialControl(new MaterialControl(this, _form, true)) {
}

QVariant HeightfieldSculptBrushTool::createEdit(bool alternate) {
    Sphere* sphere = new Sphere();
    sphere->setTranslation(_position);
    sphere->setScale(_radius->value());
    if (alternate) {
        return QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(sphere),
            SharedObjectPointer(), QColor(0, 0, 0, 0), false, false, pow(2.0f, _granularity->value())));
    } else {
        return QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(sphere),
            _materialControl->getMaterial(), _materialControl->getColor(),
            false, false, pow(2.0f, _granularity->value())));
    }
}

HeightfieldFillBrushTool::HeightfieldFillBrushTool(MetavoxelEditor* editor) :
    HeightfieldBrushTool(editor, "Fill Brush") {
    
    _form->addRow("Mode:", _mode = new QComboBox());
    _mode->addItem("Fill");
    _mode->addItem("Voxelize");
}

QVariant HeightfieldFillBrushTool::createEdit(bool alternate) {
    const int FILL_MODE_INDEX = 0;
    if (_mode->currentIndex() == FILL_MODE_INDEX) {
        return QVariant::fromValue(FillHeightfieldHeightEdit(_position, _radius->value(),
            pow(2.0f, _granularity->value())));
    }
    Sphere* sphere = new Sphere();
    sphere->setTranslation(_position);
    sphere->setScale(_radius->value());
    return QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(sphere),
        SharedObjectPointer(), QColor(), false, true, pow(2.0f, _granularity->value())));
}

HeightfieldMaterialBoxTool::HeightfieldMaterialBoxTool(MetavoxelEditor* editor) :
    BoxTool(editor, "Set Material (Box)", false) {
    
    QWidget* widget = new QWidget();
    QFormLayout* form = new QFormLayout();
    widget->setLayout(form);
    layout()->addWidget(widget);
    
    QHBoxLayout* gridLayout = new QHBoxLayout();
    gridLayout->addStretch(1);
    gridLayout->addWidget(_snapToGrid = new QCheckBox("Snap to Grid"));
    gridLayout->addStretch(1);
    form->addRow(gridLayout);
    _snapToGrid->setChecked(true);
    
    _materialControl = new MaterialControl(this, form, true);
    
    form->addRow("Granularity:", _granularity = new QDoubleSpinBox());
    _granularity->setMinimum(-FLT_MAX);
    _granularity->setMaximum(FLT_MAX);
    _granularity->setPrefix("2^");
    _granularity->setValue(8.0);
}

bool HeightfieldMaterialBoxTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

bool HeightfieldMaterialBoxTool::shouldSnapToGrid() {
    return _snapToGrid->isChecked();
}

QColor HeightfieldMaterialBoxTool::getColor() {
    return _materialControl->getColor();
}

void HeightfieldMaterialBoxTool::applyValue(const glm::vec3& minimum, const glm::vec3& maximum) {
    Cuboid* cuboid = new Cuboid();
    cuboid->setTranslation((maximum + minimum) * 0.5f);
    glm::vec3 vector = (maximum - minimum) * 0.5f;
    cuboid->setScale(vector.x);
    cuboid->setAspectY(vector.y / vector.x);
    cuboid->setAspectZ(vector.z / vector.x);
    MetavoxelEditMessage message = { QVariant::fromValue(HeightfieldMaterialSpannerEdit(SharedObjectPointer(cuboid),
        _materialControl->getMaterial(), _materialControl->getColor(), false, false, pow(2.0f, _granularity->value()))) };
    Application::getInstance()->getMetavoxels()->applyEdit(message, true);
}

HeightfieldMaterialSpannerTool::HeightfieldMaterialSpannerTool(MetavoxelEditor* editor) :
    PlaceSpannerTool(editor, "Set Material (Spanner)", QString(), false) {
    
    QWidget* widget = new QWidget();
    QFormLayout* form = new QFormLayout();
    widget->setLayout(form);
    layout()->addWidget(widget);
    
    form->addRow(_spannerEditor = new SharedObjectEditor(&Spanner::staticMetaObject, false, this));
    _spannerEditor->setObject(new Sphere());
    
    _materialControl = new MaterialControl(this, form, true);
    
    form->addRow("Granularity:", _granularity = new QDoubleSpinBox());
    _granularity->setMinimum(-FLT_MAX);
    _granularity->setMaximum(FLT_MAX);
    _granularity->setPrefix("2^");
    _granularity->setValue(8.0);
    
    QPushButton* place = new QPushButton("Set");
    layout()->addWidget(place);
    connect(place, &QPushButton::clicked, this, &HeightfieldMaterialSpannerTool::place);
}

bool HeightfieldMaterialSpannerTool::appliesTo(const AttributePointer& attribute) const {
    return attribute->inherits("SpannerSetAttribute");
}

SharedObjectPointer HeightfieldMaterialSpannerTool::getSpanner() {
    return _spannerEditor->getObject();
}

QColor HeightfieldMaterialSpannerTool::getColor() {
    return _materialControl->getColor();
}

void HeightfieldMaterialSpannerTool::applyEdit(const AttributePointer& attribute, const SharedObjectPointer& spanner) {
    static_cast<Spanner*>(spanner.data())->setWillBeVoxelized(true);
    MetavoxelEditMessage message = { QVariant::fromValue(HeightfieldMaterialSpannerEdit(spanner,
        _materialControl->getMaterial(), _materialControl->getColor(), false, false, pow(2.0f, _granularity->value()))) };
    Application::getInstance()->getMetavoxels()->applyEdit(message, true);
}

