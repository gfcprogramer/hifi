//
//  AddressBarDialog.cpp
//  interface/src/ui
//
//  Created by Stojce Slavkovski on 9/22/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QMessageBox>

#include <PathUtils.h>

#include "AddressBarDialog.h"
#include "AddressManager.h"
#include "Application.h"
#include "MainWindow.h"

const QString ADDRESSBAR_GO_BUTTON_ICON = "images/address-bar-submit.svg";
const QString ADDRESSBAR_GO_BUTTON_ACTIVE_ICON = "images/address-bar-submit-active.svg";

AddressBarDialog::AddressBarDialog(QWidget* parent) :
    FramelessDialog(parent, 0, FramelessDialog::POSITION_TOP)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setupUI();
    
    auto addressManager = DependencyManager::get<AddressManager>();
    
    connect(addressManager.data(), &AddressManager::lookupResultIsOffline, this, &AddressBarDialog::displayAddressOfflineMessage);
    connect(addressManager.data(), &AddressManager::lookupResultIsNotFound, this, &AddressBarDialog::displayAddressNotFoundMessage);
}

void AddressBarDialog::setupUI() {
    
    const QString DIALOG_STYLESHEET = "font-family: Helvetica, Arial, sans-serif;";
    const QString ADDRESSBAR_PLACEHOLDER = "Go to: domain, location, @user, /x,y,z";
    const QString ADDRESSBAR_STYLESHEET = "padding: 5px 10px; font-size: 20px;";
    
    const int ADDRESSBAR_MIN_WIDTH = 200;
    const int ADDRESSBAR_MAX_WIDTH = 615;
    const int ADDRESSBAR_HEIGHT = 42;
    const int ADDRESSBAR_STRETCH = 60;
    
    const int BUTTON_SPACER_SIZE = 5;
    const int DEFAULT_SPACER_SIZE = 20;
    const int ADDRESS_LAYOUT_RIGHT_MARGIN = 10;
    
    const int GO_BUTTON_SIZE = 42;
    const int CLOSE_BUTTON_SIZE = 16;
    const QString CLOSE_BUTTON_ICON = "styles/close.svg";
    
    const int DIALOG_HEIGHT = 62;
    const int DIALOG_INITIAL_WIDTH = 560;
    
    QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setSizePolicy(sizePolicy);
    setMinimumSize(QSize(DIALOG_INITIAL_WIDTH, DIALOG_HEIGHT));
    setMaximumHeight(DIALOG_HEIGHT);
    setStyleSheet(DIALOG_STYLESHEET);

    _verticalLayout = new QVBoxLayout(this);
    _verticalLayout->setContentsMargins(0, 0, 0, 0);
    
    _addressLayout = new QHBoxLayout();
    _addressLayout->setContentsMargins(0, 0, ADDRESS_LAYOUT_RIGHT_MARGIN, 0);
    
    _leftSpacer = new QSpacerItem(DEFAULT_SPACER_SIZE,
                                  DEFAULT_SPACER_SIZE,
                                  QSizePolicy::MinimumExpanding,
                                  QSizePolicy::Minimum);
    
    _addressLayout->addItem(_leftSpacer);
    
    _addressLineEdit = new QLineEdit(this);
    _addressLineEdit->setAttribute(Qt::WA_MacShowFocusRect, 0);
    _addressLineEdit->setPlaceholderText(ADDRESSBAR_PLACEHOLDER);
    QSizePolicy sizePolicyLineEdit(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sizePolicyLineEdit.setHorizontalStretch(ADDRESSBAR_STRETCH);
    _addressLineEdit->setSizePolicy(sizePolicyLineEdit);
    _addressLineEdit->setMinimumSize(QSize(ADDRESSBAR_MIN_WIDTH, ADDRESSBAR_HEIGHT));
    _addressLineEdit->setMaximumSize(QSize(ADDRESSBAR_MAX_WIDTH, ADDRESSBAR_HEIGHT));
    _addressLineEdit->setStyleSheet(ADDRESSBAR_STYLESHEET);
    _addressLayout->addWidget(_addressLineEdit);
    
    _buttonSpacer = new QSpacerItem(BUTTON_SPACER_SIZE, BUTTON_SPACER_SIZE, QSizePolicy::Fixed, QSizePolicy::Minimum);
    _addressLayout->addItem(_buttonSpacer);
    
    _goButton = new QPushButton(this);
    _goButton->setSizePolicy(sizePolicy);
    _goButton->setMinimumSize(QSize(GO_BUTTON_SIZE, GO_BUTTON_SIZE));
    _goButton->setMaximumSize(QSize(GO_BUTTON_SIZE, GO_BUTTON_SIZE));
    _goButton->setIcon(QIcon(PathUtils::resourcesPath() + ADDRESSBAR_GO_BUTTON_ICON));
    _goButton->setIconSize(QSize(GO_BUTTON_SIZE, GO_BUTTON_SIZE));
    _goButton->setDefault(true);
    _goButton->setFlat(true);
    _addressLayout->addWidget(_goButton);
    
    _rightSpacer = new QSpacerItem(DEFAULT_SPACER_SIZE,
                                   DEFAULT_SPACER_SIZE,
                                   QSizePolicy::MinimumExpanding,
                                   QSizePolicy::Minimum);
    
    _addressLayout->addItem(_rightSpacer);
    
    _closeButton = new QPushButton(this);
    _closeButton->setSizePolicy(sizePolicy);
    _closeButton->setMinimumSize(QSize(CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE));
    _closeButton->setMaximumSize(QSize(CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE));
    QIcon icon(PathUtils::resourcesPath() + CLOSE_BUTTON_ICON);
    _closeButton->setIcon(icon);
    _closeButton->setIconSize(QSize(CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE));
    _closeButton->setFlat(true);
    _addressLayout->addWidget(_closeButton, 0, Qt::AlignRight);
    
    _verticalLayout->addLayout(_addressLayout);
    
    connect(_goButton, &QPushButton::clicked, this, &AddressBarDialog::accept);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::close);
}

void AddressBarDialog::showEvent(QShowEvent* event) {
    _goButton->setIcon(QIcon(PathUtils::resourcesPath() + ADDRESSBAR_GO_BUTTON_ICON));
    _addressLineEdit->setText(QString());
    _addressLineEdit->setFocus();
    FramelessDialog::showEvent(event);
}

void AddressBarDialog::accept() {
    if (!_addressLineEdit->text().isEmpty()) {
        _goButton->setIcon(QIcon(PathUtils::resourcesPath() + ADDRESSBAR_GO_BUTTON_ACTIVE_ICON));
        auto addressManager = DependencyManager::get<AddressManager>();
        connect(addressManager.data(), &AddressManager::lookupResultsFinished, this, &QDialog::hide);
        addressManager->handleLookupString(_addressLineEdit->text());
    }
}

void AddressBarDialog::displayAddressOfflineMessage() {
    QMessageBox::information(Application::getInstance()->getWindow(), "Address offline",
                             "That user or place is currently offline.");
}

void AddressBarDialog::displayAddressNotFoundMessage() {
    QMessageBox::information(Application::getInstance()->getWindow(), "Address not found",
                             "There is no address information for that user or place.");
}