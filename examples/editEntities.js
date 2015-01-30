
//  newEditEntities.js
//  examples
//
//  Created by Brad Hefta-Gaub on 10/2/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  This script allows you to edit entities with a new UI/UX for mouse and trackpad based editing
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

HIFI_PUBLIC_BUCKET = "http://s3.amazonaws.com/hifi-public/";

Script.include([
    "libraries/stringHelpers.js",
    "libraries/dataviewHelpers.js",
    "libraries/httpMultiPart.js",
    "libraries/modelUploader.js",
    "libraries/toolBars.js",
    "libraries/progressDialog.js",

    "libraries/entitySelectionTool.js",
    "libraries/ModelImporter.js",

    "libraries/ExportMenu.js",
    "libraries/ToolTip.js",

    "libraries/entityPropertyDialogBox.js",
    "libraries/entityCameraTool.js",
    "libraries/gridTool.js",
    "libraries/entityList.js",
]);

var selectionDisplay = SelectionDisplay;
var selectionManager = SelectionManager;
var modelImporter = new ModelImporter();
var entityPropertyDialogBox = EntityPropertyDialogBox;

var cameraManager = new CameraManager();

var grid = Grid();
gridTool = GridTool({ horizontalGrid: grid });
gridTool.setVisible(false);

var entityListTool = EntityListTool();

var hasShownPropertiesTool = false;

var entityListVisible = false;

selectionManager.addEventListener(function() {
    selectionDisplay.updateHandles();
    if (selectionManager.hasSelection() && !hasShownPropertiesTool) {
        // Open properties and model list, but force selection of model list tab
        propertiesTool.setVisible(false);
        entityListTool.setVisible(false);
        gridTool.setVisible(false);
        propertiesTool.setVisible(true);
        entityListTool.setVisible(true);
        gridTool.setVisible(true);
        hasShownPropertiesTool = true;
    }
    if (!selectionManager.hasSelection()) {
        toolBar.setActive(false);
    } else {
        toolBar.setActive(true);
    }
});

var windowDimensions = Controller.getViewportDimensions();
var toolIconUrl = HIFI_PUBLIC_BUCKET + "images/tools/";
var toolHeight = 50;
var toolWidth = 50;

var MIN_ANGULAR_SIZE = 2;
var MAX_ANGULAR_SIZE = 45;
var allowLargeModels = true;
var allowSmallModels = true;
var wantEntityGlow = false;

var SPAWN_DISTANCE = 1;
var DEFAULT_DIMENSION = 0.20;
var DEFAULT_TEXT_DIMENSION_X = 1.0;
var DEFAULT_TEXT_DIMENSION_Y = 1.0;
var DEFAULT_TEXT_DIMENSION_Z = 0.01;

var DEFAULT_DIMENSIONS = {
    x: DEFAULT_DIMENSION,
    y: DEFAULT_DIMENSION,
    z: DEFAULT_DIMENSION
};

var MENU_INSPECT_TOOL_ENABLED = "Inspect Tool";
var MENU_EASE_ON_FOCUS = "Ease Orientation on Focus";

var SETTING_INSPECT_TOOL_ENABLED = "inspectToolEnabled";
var SETTING_EASE_ON_FOCUS = "cameraEaseOnFocus";

var modelURLs = [
        HIFI_PUBLIC_BUCKET + "models/entities/2-Terrain:%20Alder.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/2-Terrain:%20Bush1.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/2-Terrain:%20Bush6.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/3-Buildings-1-Rustic-Shed.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/3-Buildings-1-Rustic-Shed2.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/3-Buildings-1-Rustic-Shed4.fbx",
        HIFI_PUBLIC_BUCKET + "models/entities/3-Buildings-1-Rustic-Shed7.fbx"
    ];

var mode = 0;
var isActive = false;


var toolBar = (function () {
    var that = {},
        toolBar,
        activeButton,
        newModelButton,
        newCubeButton,
        newSphereButton,
        newLightButton,
        newTextButton,
        browseModelsButton,
        loadURLMenuItem,
        loadFileMenuItem,
        menuItemWidth,
        menuItemOffset,
        menuItemHeight,
        menuItemMargin = 5,
        menuTextColor = { red: 255, green: 255, blue: 255 },
        menuBackgroundColor = { red: 18, green: 66, blue: 66 };

    function initialize() {
        toolBar = new ToolBar(0, 0, ToolBar.VERTICAL);

        // Hide active button for now - this may come back, so not deleting yet.
        activeButton = toolBar.addTool({
            imageURL: toolIconUrl + "models-tool.svg",
            // subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: 0, height: 0 },
            width: 0,//toolWidth,
            height: 0,//toolHeight,
            alpha: 0.9,
            visible: true
        }, true, false);

        newModelButton = toolBar.addTool({
            imageURL: toolIconUrl + "add-model-tool.svg",
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        }, true, false);

        browseModelsButton = toolBar.addTool({
            imageURL: toolIconUrl + "list-icon.svg",
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        });

        menuItemOffset = toolBar.height / 3 + 2;
        menuItemHeight = Tool.IMAGE_HEIGHT / 2 - 2;

        loadURLMenuItem = Overlays.addOverlay("text", {
            height: menuItemHeight,
            backgroundColor: menuBackgroundColor,
            topMargin: menuItemMargin,
            text: "Model URL",
            alpha: 0.9,
            backgroundAlpha: 0.9,
            visible: false
        });

        loadFileMenuItem = Overlays.addOverlay("text", {
            height: menuItemHeight,
            backgroundColor: menuBackgroundColor,
            topMargin: menuItemMargin,
            text: "Model File",
            alpha: 0.9,
            backgroundAlpha: 0.9,
            visible: false
        });

        menuItemWidth = Math.max(Overlays.textSize(loadURLMenuItem, "Model URL").width,
            Overlays.textSize(loadFileMenuItem, "Model File").width) + 20;
        Overlays.editOverlay(loadURLMenuItem, { width: menuItemWidth });
        Overlays.editOverlay(loadFileMenuItem, { width: menuItemWidth });

        newCubeButton = toolBar.addTool({
            imageURL: toolIconUrl + "add-cube.svg",
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        });

        newSphereButton = toolBar.addTool({
            imageURL: toolIconUrl + "add-sphere.svg",
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        });

        newLightButton = toolBar.addTool({
            imageURL: toolIconUrl + "light.svg",
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        });

        newTextButton = toolBar.addTool({
            imageURL: toolIconUrl + "add-text.svg",
            subImage: { x: 0, y: Tool.IMAGE_WIDTH, width: Tool.IMAGE_WIDTH, height: Tool.IMAGE_HEIGHT },
            width: toolWidth,
            height: toolHeight,
            alpha: 0.9,
            visible: true
        });

    }

    function toggleNewModelButton(active) {
        if (active === undefined) {
            active = !toolBar.toolSelected(newModelButton);
        }
        toolBar.selectTool(newModelButton, active);

        Overlays.editOverlay(loadURLMenuItem, { visible: active });
        Overlays.editOverlay(loadFileMenuItem, { visible: active });
    }


    that.setActive = function(active) {
        if (active != isActive) {
            isActive = active;
            if (!isActive) {
                entityListTool.setVisible(false);
                gridTool.setVisible(false);
                grid.setEnabled(false);
                propertiesTool.setVisible(false);
                selectionManager.clearSelections();
                cameraManager.disable();
            } else {
                hasShownPropertiesTool = false;
                cameraManager.enable();
                grid.setEnabled(true);
            }
        }
        toolBar.selectTool(activeButton, active);
    };

    var RESIZE_INTERVAL = 50;
    var RESIZE_TIMEOUT = 20000;
    var RESIZE_MAX_CHECKS = RESIZE_TIMEOUT / RESIZE_INTERVAL;
    function addModel(url) {
        var position;

        position = Vec3.sum(MyAvatar.position, Vec3.multiply(Quat.getFront(MyAvatar.orientation), SPAWN_DISTANCE));

        if (position.x > 0 && position.y > 0 && position.z > 0) {
            var entityId = Entities.addEntity({
                type: "Model",
                position: grid.snapToSurface(grid.snapToGrid(position, false, DEFAULT_DIMENSIONS), DEFAULT_DIMENSIONS),
                dimensions: DEFAULT_DIMENSIONS,
                modelURL: url
            });
            print("Model added: " + url);

            var checkCount = 0;
            function resize() {
                var entityProperties = Entities.getEntityProperties(entityId);
                var naturalDimensions = entityProperties.naturalDimensions;

                checkCount++;

                if (naturalDimensions.x == 0 && naturalDimensions.y == 0 && naturalDimensions.z == 0) {
                    if (checkCount < RESIZE_MAX_CHECKS) {
                        Script.setTimeout(resize, RESIZE_INTERVAL);
                    } else {
                        print("Resize failed: timed out waiting for model (" + url + ") to load");
                    }
                } else {
                    entityProperties.dimensions = naturalDimensions;
                    Entities.editEntity(entityId, entityProperties);
                }
            }

            Script.setTimeout(resize, RESIZE_INTERVAL);
        } else {
            print("Can't add model: Model would be out of bounds.");
        }
    }

    that.move = function () {
        var newViewPort,
            toolsX,
            toolsY;

        newViewPort = Controller.getViewportDimensions();

        if (toolBar === undefined) {
            initialize();

        } else if (windowDimensions.x === newViewPort.x &&
                   windowDimensions.y === newViewPort.y) {
            return;
        }

        windowDimensions = newViewPort;
        toolsX = windowDimensions.x - 8 - toolBar.width;
        toolsY = (windowDimensions.y - toolBar.height) / 2;

        toolBar.move(toolsX, toolsY);

        Overlays.editOverlay(loadURLMenuItem, { x: toolsX - menuItemWidth, y: toolsY + menuItemOffset });
        Overlays.editOverlay(loadFileMenuItem, { x: toolsX - menuItemWidth, y: toolsY + menuItemOffset + menuItemHeight });
    };

    that.mousePressEvent = function (event) {
        var clickedOverlay,
            url,
            file;

        clickedOverlay = Overlays.getOverlayAtPoint({ x: event.x, y: event.y });

        if (activeButton === toolBar.clicked(clickedOverlay)) {
            that.setActive(!isActive);
            return true;
        }

        if (newModelButton === toolBar.clicked(clickedOverlay)) {
            toggleNewModelButton();
            return true;
        }

        if (clickedOverlay === loadURLMenuItem) {
            toggleNewModelButton(false);
            url = Window.prompt("Model URL", modelURLs[Math.floor(Math.random() * modelURLs.length)]);
            if (url !== null && url !== "") {
                addModel(url);
            }
            return true;
        }

        if (clickedOverlay === loadFileMenuItem) {
            toggleNewModelButton(false);

            file = Window.browse("Select your model file ...",
                Settings.getValue("LastModelUploadLocation").path(),
                "Model files (*.fst *.fbx)");
                //"Model files (*.fst *.fbx *.svo)");
            if (file !== null) {
                Settings.setValue("LastModelUploadLocation", file);
                modelUploader.upload(file, addModel);
            }
            return true;
        }

        if (browseModelsButton === toolBar.clicked(clickedOverlay)) {
            toggleNewModelButton(false);
            url = Window.s3Browse(".*(fbx|FBX)");
            if (url !== null && url !== "") {
                addModel(url);
            }
            return true;
        }

        if (newCubeButton === toolBar.clicked(clickedOverlay)) {
            var position = Vec3.sum(MyAvatar.position, Vec3.multiply(Quat.getFront(MyAvatar.orientation), SPAWN_DISTANCE));

            if (position.x > 0 && position.y > 0 && position.z > 0) {
                Entities.addEntity({
                                type: "Box",
                                position: grid.snapToSurface(grid.snapToGrid(position, false, DEFAULT_DIMENSIONS), DEFAULT_DIMENSIONS),
                                dimensions: DEFAULT_DIMENSIONS,
                                color: { red: 255, green: 0, blue: 0 }

                                });
            } else {
                print("Can't create box: Box would be out of bounds.");
            }
            return true;
        }

        if (newSphereButton === toolBar.clicked(clickedOverlay)) {
            var position = Vec3.sum(MyAvatar.position, Vec3.multiply(Quat.getFront(MyAvatar.orientation), SPAWN_DISTANCE));

            if (position.x > 0 && position.y > 0 && position.z > 0) {
                Entities.addEntity({
                                type: "Sphere",
                                position: grid.snapToSurface(grid.snapToGrid(position, false, DEFAULT_DIMENSIONS), DEFAULT_DIMENSIONS),
                                dimensions: DEFAULT_DIMENSIONS,
                                color: { red: 255, green: 0, blue: 0 }
                                });
            } else {
                print("Can't create sphere: Sphere would be out of bounds.");
            }
            return true;
        }

        if (newLightButton === toolBar.clicked(clickedOverlay)) {
            var position = Vec3.sum(MyAvatar.position, Vec3.multiply(Quat.getFront(MyAvatar.orientation), SPAWN_DISTANCE));

            if (position.x > 0 && position.y > 0 && position.z > 0) {
                Entities.addEntity({
                                type: "Light",
                                position: grid.snapToSurface(grid.snapToGrid(position, false, DEFAULT_DIMENSIONS), DEFAULT_DIMENSIONS),
                                dimensions: DEFAULT_DIMENSIONS,
                                isSpotlight: false,
                                diffuseColor: { red: 255, green: 255, blue: 255 },
                                ambientColor: { red: 255, green: 255, blue: 255 },
                                specularColor: { red: 0, green: 0, blue: 0 },

                                constantAttenuation: 1,
                                linearAttenuation: 0,
                                quadraticAttenuation: 0,
                                exponent: 0,
                                cutoff: 180, // in degrees
                                });
            } else {
                print("Can't create Light: Light would be out of bounds.");
            }
            return true;
        }

        if (newTextButton === toolBar.clicked(clickedOverlay)) {
            var position = Vec3.sum(MyAvatar.position, Vec3.multiply(Quat.getFront(MyAvatar.orientation), SPAWN_DISTANCE));

            if (position.x > 0 && position.y > 0 && position.z > 0) {
                Entities.addEntity({ 
                                type: "Text",
                                position: grid.snapToSurface(grid.snapToGrid(position, false, DEFAULT_DIMENSIONS), DEFAULT_DIMENSIONS),
                                dimensions: DEFAULT_DIMENSIONS,
                                backgroundColor: { red: 0, green: 0, blue: 0 },
                                textColor: { red: 255, green: 255, blue: 255 },
                                text: "some text",
                                lineHight: "0.1"
                                });
            } else {
                print("Can't create box: Text would be out of bounds.");
            }
            return true;
        }


        return false;
    };

    that.cleanup = function () {
        toolBar.cleanup();
        Overlays.deleteOverlay(loadURLMenuItem);
        Overlays.deleteOverlay(loadFileMenuItem);
    };

    return that;
}());


var exportMenu = null;

function isLocked(properties) {
    // special case to lock the ground plane model in hq.
    if (location.hostname == "hq.highfidelity.io" &&
        properties.modelURL == HIFI_PUBLIC_BUCKET + "ozan/Terrain_Reduce_forAlpha.fbx") {
        return true;
    }
    return false;
}


var selectedEntityID;
var orientation;
var intersection;


var SCALE_FACTOR = 200.0;

function rayPlaneIntersection(pickRay, point, normal) {
    var d = -Vec3.dot(point, normal);
    var t = -(Vec3.dot(pickRay.origin, normal) + d) / Vec3.dot(pickRay.direction, normal);

    return Vec3.sum(pickRay.origin, Vec3.multiply(pickRay.direction, t));
}

function findClickedEntity(event) {
    var pickRay = Camera.computePickRay(event.x, event.y);

    var foundIntersection = Entities.findRayIntersection(pickRay, true); // want precision picking

    if (!foundIntersection.accurate) {
        return null;
    }
    var foundEntity = foundIntersection.entityID;

    if (!foundEntity.isKnownID) {
        var identify = Entities.identifyEntity(foundEntity);
        if (!identify.isKnownID) {
            print("Unknown ID " + identify.id + " (update loop " + foundEntity.id + ")");
            return null;
        }
        foundEntity = identify;
    }

    return { pickRay: pickRay, entityID: foundEntity };
}

var mouseHasMovedSincePress = false;

function mousePressEvent(event) {
    mouseHasMovedSincePress = false;
    mouseCapturedByTool = false;

    if (toolBar.mousePressEvent(event) || progressDialog.mousePressEvent(event)) {
        mouseCapturedByTool = true;
        return;
    }
    if (isActive) {
        if (cameraManager.mousePressEvent(event) || selectionDisplay.mousePressEvent(event)) {
            // Event handled; do nothing.
            return;
        }
    } else if (Menu.isOptionChecked(MENU_INSPECT_TOOL_ENABLED)) {
        var result = findClickedEntity(event);
        if (event.isRightButton) {
            if (result !== null) {
                var currentProperties = Entities.getEntityProperties(result.entityID);
                cameraManager.enable();
                cameraManager.focus(currentProperties.position, null, Menu.isOptionChecked(MENU_EASE_ON_FOCUS));
                cameraManager.mousePressEvent(event);
            }
        } else {
            cameraManager.mousePressEvent(event);
        }
    }
}

var highlightedEntityID = { isKnownID: false };
var mouseCapturedByTool = false;
var lastMousePosition = null;
var idleMouseTimerId = null;
var IDLE_MOUSE_TIMEOUT = 200;

function mouseMoveEvent(event) {
    if (idleMouseTimerId) {
        Script.clearTimeout(idleMouseTimerId);
    }

    mouseHasMovedSincePress = true;

    // allow the selectionDisplay and cameraManager to handle the event first, if it doesn't handle it, then do our own thing
    if (selectionDisplay.mouseMoveEvent(event) || cameraManager.mouseMoveEvent(event)) {
        return;
    }

    lastMousePosition = { x: event.x, y: event.y };

    highlightEntityUnderCursor(lastMousePosition, false);
    idleMouseTimerId = Script.setTimeout(handleIdleMouse, IDLE_MOUSE_TIMEOUT);
}

function handleIdleMouse() {
    idleMouseTimerId = null;
    highlightEntityUnderCursor(lastMousePosition, true);
}

function highlightEntityUnderCursor(position, accurateRay) {
    var pickRay = Camera.computePickRay(position.x, position.y);
    var entityIntersection = Entities.findRayIntersection(pickRay, accurateRay);
    if (entityIntersection.accurate) {
        if(highlightedEntityID.isKnownID && highlightedEntityID.id != entityIntersection.entityID.id) {
            selectionDisplay.unhighlightSelectable(highlightedEntityID);
            highlightedEntityID = { id: -1, isKnownID: false };
        }

        var halfDiagonal = Vec3.length(entityIntersection.properties.dimensions) / 2.0;

        var angularSize = 2 * Math.atan(halfDiagonal / Vec3.distance(Camera.getPosition(),
                                        entityIntersection.properties.position)) * 180 / 3.14;

        var sizeOK = (allowLargeModels || angularSize < MAX_ANGULAR_SIZE)
                        && (allowSmallModels || angularSize > MIN_ANGULAR_SIZE);

        if (entityIntersection.entityID.isKnownID && sizeOK) {
            if (wantEntityGlow) {
                Entities.editEntity(entityIntersection.entityID, { glowLevel: 0.25 });
            }
            highlightedEntityID = entityIntersection.entityID;
            selectionDisplay.highlightSelectable(entityIntersection.entityID);
        }

    }
}


function mouseReleaseEvent(event) {
    if (isActive && selectionManager.hasSelection()) {
        tooltip.show(false);
    }
    if (mouseCapturedByTool) {
        return;
    }

    cameraManager.mouseReleaseEvent(event);

    if (!mouseHasMovedSincePress) {
        mouseClickEvent(event);
    }
}

function mouseClickEvent(event) {
    if (!event.isRightButton) {
        return;
    }

    var result = findClickedEntity(event);
    if (result === null) {
        if (!event.isShifted) {
            selectionManager.clearSelections();
        }
        return;
    }
    toolBar.setActive(true);
    var pickRay = result.pickRay;
    var foundEntity = result.entityID;

    var properties = Entities.getEntityProperties(foundEntity);
    if (isLocked(properties)) {
        print("Model locked " + properties.id);
    } else {
        var halfDiagonal = Vec3.length(properties.dimensions) / 2.0;

        print("Checking properties: " + properties.id + " " + properties.isKnownID + " - Half Diagonal:" + halfDiagonal);
        //                P         P - Model
        //               /|         A - Palm
        //              / | d       B - unit vector toward tip
        //             /  |         X - base of the perpendicular line
        //            A---X----->B  d - distance fom axis
        //              x           x - distance from A
        //
        //            |X-A| = (P-A).B
        //            X == A + ((P-A).B)B
        //            d = |P-X|

        var A = pickRay.origin;
        var B = Vec3.normalize(pickRay.direction);
        var P = properties.position;

        var x = Vec3.dot(Vec3.subtract(P, A), B);
        var X = Vec3.sum(A, Vec3.multiply(B, x));
        var d = Vec3.length(Vec3.subtract(P, X));
        var halfDiagonal = Vec3.length(properties.dimensions) / 2.0;

        var angularSize = 2 * Math.atan(halfDiagonal / Vec3.distance(Camera.getPosition(), properties.position)) * 180 / 3.14;

        var sizeOK = (allowLargeModels || angularSize < MAX_ANGULAR_SIZE)
                        && (allowSmallModels || angularSize > MIN_ANGULAR_SIZE);

        if (0 < x && sizeOK) {
            entitySelected = true;
            selectedEntityID = foundEntity;
            orientation = MyAvatar.orientation;
            intersection = rayPlaneIntersection(pickRay, P, Quat.getFront(orientation));

            if (!event.isShifted) {
                selectionManager.clearSelections();
            }

            var toggle = event.isShifted;
            selectionManager.addEntity(foundEntity, toggle);

            print("Model selected: " + foundEntity.id);
            selectionDisplay.select(selectedEntityID, event);

            cameraManager.focus(selectionManager.worldPosition,
                                selectionManager.worldDimensions,
                                Menu.isOptionChecked(MENU_EASE_ON_FOCUS));
        }
    }
}

Controller.mousePressEvent.connect(mousePressEvent);
Controller.mouseMoveEvent.connect(mouseMoveEvent);
Controller.mouseReleaseEvent.connect(mouseReleaseEvent);


// In order for editVoxels and editModels to play nice together, they each check to see if a "delete" menu item already
// exists. If it doesn't they add it. If it does they don't. They also only delete the menu item if they were the one that
// added it.
var modelMenuAddedDelete = false;
var originalLightsArePickable = Entities.getLightsArePickable();
function setupModelMenus() {
    print("setupModelMenus()");
    // adj our menuitems
    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Models", isSeparator: true, beforeItem: "Physics" });
    if (!Menu.menuItemExists("Edit", "Delete")) {
        print("no delete... adding ours");
        Menu.addMenuItem({ menuName: "Edit", menuItemName: "Delete",
            shortcutKeyEvent: { text: "backspace" }, afterItem: "Models" });
        modelMenuAddedDelete = true;
    } else {
        print("delete exists... don't add ours");
    }

    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Entity List...", shortcutKey: "CTRL+META+L", afterItem: "Models" });
    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Paste Models", shortcutKey: "CTRL+META+V", afterItem: "Entity List..." });
    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Allow Selecting of Large Models", shortcutKey: "CTRL+META+L", 
                        afterItem: "Paste Models", isCheckable: true, isChecked: true });
    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Allow Selecting of Small Models", shortcutKey: "CTRL+META+S", 
                        afterItem: "Allow Selecting of Large Models", isCheckable: true, isChecked: true });
    Menu.addMenuItem({ menuName: "Edit", menuItemName: "Allow Selecting of Lights", shortcutKey: "CTRL+SHIFT+META+L", 
                        afterItem: "Allow Selecting of Small Models", isCheckable: true });

    Menu.addMenuItem({ menuName: "File", menuItemName: "Models", isSeparator: true, beforeItem: "Settings" });
    Menu.addMenuItem({ menuName: "File", menuItemName: "Export Models", shortcutKey: "CTRL+META+E", afterItem: "Models" });
    Menu.addMenuItem({ menuName: "File", menuItemName: "Import Models", shortcutKey: "CTRL+META+I", afterItem: "Export Models" });


    Menu.addMenuItem({ menuName: "View", menuItemName: MENU_EASE_ON_FOCUS, afterItem: MENU_INSPECT_TOOL_ENABLED,
                       isCheckable: true, isChecked: Settings.getValue(SETTING_EASE_ON_FOCUS) == "true" });

    Entities.setLightsArePickable(false);
}

setupModelMenus(); // do this when first running our script.

function cleanupModelMenus() {
    Menu.removeSeparator("Edit", "Models");
    if (modelMenuAddedDelete) {
        // delete our menuitems
        Menu.removeMenuItem("Edit", "Delete");
    }

    Menu.removeMenuItem("Edit", "Entity List...");
    Menu.removeMenuItem("Edit", "Paste Models");
    Menu.removeMenuItem("Edit", "Allow Selecting of Large Models");
    Menu.removeMenuItem("Edit", "Allow Selecting of Small Models");
    Menu.removeMenuItem("Edit", "Allow Selecting of Lights");

    Menu.removeSeparator("File", "Models");
    Menu.removeMenuItem("File", "Export Models");
    Menu.removeMenuItem("File", "Import Models");

    Menu.removeMenuItem("View", MENU_INSPECT_TOOL_ENABLED);
    Menu.removeMenuItem("View", MENU_EASE_ON_FOCUS);
}

Script.scriptEnding.connect(function() {
    Settings.setValue(SETTING_INSPECT_TOOL_ENABLED, Menu.isOptionChecked(MENU_INSPECT_TOOL_ENABLED));
    Settings.setValue(SETTING_EASE_ON_FOCUS, Menu.isOptionChecked(MENU_EASE_ON_FOCUS));

    progressDialog.cleanup();
    toolBar.cleanup();
    cleanupModelMenus();
    tooltip.cleanup();
    modelImporter.cleanup();
    selectionDisplay.cleanup();
    if (exportMenu) {
        exportMenu.close();
    }
    Entities.setLightsArePickable(originalLightsArePickable);
});

// Do some stuff regularly, like check for placement of various overlays
Script.update.connect(function (deltaTime) {
    toolBar.move();
    progressDialog.move();
    selectionDisplay.checkMove();
});

function deleteSelectedEntities() {
    if (SelectionManager.hasSelection()) {
        print("  Delete Entities");
        SelectionManager.saveProperties();
        var savedProperties = [];
        for (var i = 0; i < selectionManager.selections.length; i++) {
            var entityID = SelectionManager.selections[i];
            var initialProperties = SelectionManager.savedProperties[entityID.id];
            SelectionManager.savedProperties[entityID.id];
            savedProperties.push({
                entityID: entityID,
                properties: initialProperties
            });
            Entities.deleteEntity(entityID);
        }
        SelectionManager.clearSelections();
        pushCommandForSelections([], savedProperties);
    } else {
        print("  Delete Entity.... not holding...");
    }
}

function handeMenuEvent(menuItem) {
    if (menuItem == "Allow Selecting of Small Models") {
        allowSmallModels = Menu.isOptionChecked("Allow Selecting of Small Models");
    } else if (menuItem == "Allow Selecting of Large Models") {
        allowLargeModels = Menu.isOptionChecked("Allow Selecting of Large Models");
    } else if (menuItem == "Allow Selecting of Lights") {
        Entities.setLightsArePickable(Menu.isOptionChecked("Allow Selecting of Lights"));
    } else if (menuItem == "Delete") {
        deleteSelectedEntities();
    } else if (menuItem == "Paste Models") {
        modelImporter.paste();
    } else if (menuItem == "Export Models") {
        if (!exportMenu) {
            exportMenu = new ExportMenu({
                onClose: function () {
                    exportMenu = null;
                }
            });
        }
    } else if (menuItem == "Import Models") {
        modelImporter.doImport();
    } else if (menuItem == "Entity List...") {
        if (isActive) {
            entityListTool.toggleVisible();
        }
    }
    tooltip.show(false);
}

Menu.menuItemEvent.connect(handeMenuEvent);

Controller.keyPressEvent.connect(function(event) {
    if (isActive) {
        cameraManager.keyPressEvent(event);
    }
});

Controller.keyReleaseEvent.connect(function (event) {
    if (isActive) {
        cameraManager.keyReleaseEvent(event);
    }
    // since sometimes our menu shortcut keys don't work, trap our menu items here also and fire the appropriate menu items
    if (event.text == "BACKSPACE" || event.text == "DELETE") {
        deleteSelectedEntities();
    } else if (event.text == "ESC") {
        selectionManager.clearSelections();
    } else if (event.text == "TAB") {
        selectionDisplay.toggleSpaceMode();
    } else if (event.text == "f") {
        if (isActive) {
            if (selectionManager.hasSelection()) {
                cameraManager.focus(selectionManager.worldPosition,
                                    selectionManager.worldDimensions,
                                    Menu.isOptionChecked(MENU_EASE_ON_FOCUS));
            }
        }
    } else if (event.text == '[') {
        if (isActive) {
            cameraManager.enable();
        }
    } else if (event.text == 'g') {
        if (isActive && selectionManager.hasSelection()) {
            var newPosition = selectionManager.worldPosition;
            newPosition = Vec3.subtract(newPosition, { x: 0, y: selectionManager.worldDimensions.y * 0.5, z: 0 });
            grid.setPosition(newPosition);
        }
    }
});

// When an entity has been deleted we need a way to "undo" this deletion.  Because it's not currently
// possible to create an entity with a specific id, earlier undo commands to the deleted entity
// will fail if there isn't a way to find the new entity id.
DELETED_ENTITY_MAP = {
}

function applyEntityProperties(data) {
    var properties = data.setProperties;
    var selectedEntityIDs = [];
    for (var i = 0; i < properties.length; i++) {
        var entityID = properties[i].entityID;
        if (DELETED_ENTITY_MAP[entityID.id] !== undefined) {
            entityID = DELETED_ENTITY_MAP[entityID.id];
        }
        Entities.editEntity(entityID, properties[i].properties);
        selectedEntityIDs.push(entityID);
    }
    for (var i = 0; i < data.createEntities.length; i++) {
        var entityID = data.createEntities[i].entityID;
        var properties = data.createEntities[i].properties;
        var newEntityID = Entities.addEntity(properties);
        DELETED_ENTITY_MAP[entityID.id] = newEntityID;
        if (data.selectCreated) {
            selectedEntityIDs.push(newEntityID);
        }
    }
    for (var i = 0; i < data.deleteEntities.length; i++) {
        var entityID = data.deleteEntities[i].entityID;
        if (DELETED_ENTITY_MAP[entityID.id] !== undefined) {
            entityID = DELETED_ENTITY_MAP[entityID.id];
        }
        Entities.deleteEntity(entityID);
    }

    selectionManager.setSelections(selectedEntityIDs);
};

// For currently selected entities, push a command to the UndoStack that uses the current entity properties for the
// redo command, and the saved properties for the undo command.  Also, include create and delete entity data.
function pushCommandForSelections(createdEntityData, deletedEntityData) {
    var undoData = {
        setProperties: [],
        createEntities: deletedEntityData || [],
        deleteEntities: createdEntityData || [],
        selectCreated: true,
    };
    var redoData = {
        setProperties: [],
        createEntities: createdEntityData || [],
        deleteEntities: deletedEntityData || [],
        selectCreated: false,
    };
    for (var i = 0; i < SelectionManager.selections.length; i++) {
        var entityID = SelectionManager.selections[i];
        var initialProperties = SelectionManager.savedProperties[entityID.id];
        var currentProperties = Entities.getEntityProperties(entityID);
        undoData.setProperties.push({
            entityID: entityID,
            properties: {
                position: initialProperties.position,
                rotation: initialProperties.rotation,
                dimensions: initialProperties.dimensions,
            },
        });
        redoData.setProperties.push({
            entityID: entityID,
            properties: {
                position: currentProperties.position,
                rotation: currentProperties.rotation,
                dimensions: currentProperties.dimensions,
            },
        });
    }
    UndoStack.pushCommand(applyEntityProperties, undoData, applyEntityProperties, redoData);
}

PropertiesTool = function(opts) {
    var that = {};

    var url = Script.resolvePath('html/entityProperties.html');
    var webView = new WebWindow('Entity Properties', url, 200, 280);

    var visible = false;

    webView.setVisible(visible);

    that.setVisible = function(newVisible) {
        visible = newVisible;
        webView.setVisible(visible);
    };

    selectionManager.addEventListener(function() {
        data = {
            type: 'update',
        };
        if (selectionManager.hasSelection()) {
            data.id = selectionManager.selections[0].id;
            data.properties = Entities.getEntityProperties(selectionManager.selections[0]);
            data.properties.rotation = Quat.safeEulerAngles(data.properties.rotation);
        }
        webView.eventBridge.emitScriptEvent(JSON.stringify(data));
    });

    webView.eventBridge.webEventReceived.connect(function(data) {
        print(data);
        data = JSON.parse(data);
        if (data.type == "update") {
            selectionManager.saveProperties();
            if (data.properties.rotation !== undefined) {
                var rotation = data.properties.rotation;
                data.properties.rotation = Quat.fromPitchYawRollDegrees(rotation.x, rotation.y, rotation.z);
            }
            Entities.editEntity(selectionManager.selections[0], data.properties);
            pushCommandForSelections();
            selectionManager._update();
        } else if (data.type == "action") {
            if (data.action == "moveSelectionToGrid") {
                if (selectionManager.hasSelection()) {
                    selectionManager.saveProperties();
                    var dY = grid.getOrigin().y - (selectionManager.worldPosition.y - selectionManager.worldDimensions.y / 2),
                    var diff = { x: 0, y: dY, z: 0 };
                    for (var i = 0; i < selectionManager.selections.length; i++) {
                        var properties = selectionManager.savedProperties[selectionManager.selections[i].id];
                        var newPosition = Vec3.sum(properties.position, diff);
                        Entities.editEntity(selectionManager.selections[i], {
                            position: newPosition,
                        });
                    }
                    pushCommandForSelections();
                    selectionManager._update();
                }
            } else if (data.action == "moveAllToGrid") {
                if (selectionManager.hasSelection()) {
                    selectionManager.saveProperties();
                    for (var i = 0; i < selectionManager.selections.length; i++) {
                        var properties = selectionManager.savedProperties[selectionManager.selections[i].id];
                        var bottomY = properties.boundingBox.center.y - properties.boundingBox.dimensions.y / 2;
                        var dY = grid.getOrigin().y - bottomY;
                        var diff = { x: 0, y: dY, z: 0 };
                        var newPosition = Vec3.sum(properties.position, diff);
                        Entities.editEntity(selectionManager.selections[i], {
                            position: newPosition,
                        });
                    }
                    pushCommandForSelections();
                    selectionManager._update();
                }
            } else if (data.action == "resetToNaturalDimensions") {
                if (selectionManager.hasSelection()) {
                    selectionManager.saveProperties();
                    for (var i = 0; i < selectionManager.selections.length; i++) {
                        var properties = selectionManager.savedProperties[selectionManager.selections[i].id];
                        Entities.editEntity(selectionManager.selections[i], {
                            dimensions: properties.naturalDimensions,
                        });
                    }
                    pushCommandForSelections();
                    selectionManager._update();
                }
            } else if (data.action == "rescaleDimensions") {
                var multiplier = data.percentage / 100;
                if (selectionManager.hasSelection()) {
                    selectionManager.saveProperties();
                    for (var i = 0; i < selectionManager.selections.length; i++) {
                        var properties = selectionManager.savedProperties[selectionManager.selections[i].id];
                        Entities.editEntity(selectionManager.selections[i], {
                            dimensions: Vec3.multiply(multiplier, properties.dimensions),
                        });
                    }
                    pushCommandForSelections();
                    selectionManager._update();
                }
            }
        }
    });

    return that;
};

propertiesTool = PropertiesTool();
