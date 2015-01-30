//
//  LoginDialog.cpp
//  interface/src/ui
//
//  Created by Ryan Huffman on 4/23/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//


#include <QWidget>
#include <QPushButton>
#include <QPixmap>

#include <PathUtils.h>

#include "Application.h"
#include "Menu.h"
#include "AccountManager.h"
#include "ui_loginDialog.h"
#include "LoginDialog.h"

const QString FORGOT_PASSWORD_URL = "https://data.highfidelity.io/users/password/new";

LoginDialog::LoginDialog(QWidget* parent) :
    FramelessDialog(parent, 0, FramelessDialog::POSITION_TOP),
    _ui(new Ui::LoginDialog) {

    _ui->setupUi(this);
    reset();
        
    setAttribute(Qt::WA_DeleteOnClose, false);

    connect(&AccountManager::getInstance(), &AccountManager::loginComplete,
            this, &LoginDialog::handleLoginCompleted);
    connect(&AccountManager::getInstance(), &AccountManager::loginFailed,
            this, &LoginDialog::handleLoginFailed);
    connect(_ui->loginButton, &QPushButton::clicked,
            this, &LoginDialog::handleLoginClicked);
    connect(_ui->closeButton, &QPushButton::clicked,
            this, &LoginDialog::close);
};

LoginDialog::~LoginDialog() {
    delete _ui;
};

void LoginDialog::reset() {
    _ui->errorLabel->hide();
    _ui->emailLineEdit->setFocus();
    _ui->logoLabel->setPixmap(QPixmap(PathUtils::resourcesPath() + "images/hifi-logo.svg"));
    _ui->loginButton->setIcon(QIcon(PathUtils::resourcesPath() + "images/login.svg"));
    _ui->infoLabel->setVisible(false);
    _ui->errorLabel->setVisible(false);
    
    _ui->emailLineEdit->setText("");
    _ui->passwordLineEdit->setText("");
    _ui->loginArea->setDisabled(false);
}

void LoginDialog::handleLoginCompleted(const QUrl& authURL) {
    reset();
    close();
};

void LoginDialog::handleLoginFailed() {
    _ui->infoLabel->setVisible(false);
    _ui->errorLabel->setVisible(true);

    _ui->errorLabel->show();
    _ui->loginArea->setDisabled(false);

    // Move focus to password and select the entire line
    _ui->passwordLineEdit->setFocus();
    _ui->passwordLineEdit->setSelection(0, _ui->emailLineEdit->maxLength());
};

void LoginDialog::handleLoginClicked() {
    // If the email or password inputs are empty, move focus to them, otherwise attempt to login.
    if (_ui->emailLineEdit->text().isEmpty()) {
        _ui->emailLineEdit->setFocus();
    } else if (_ui->passwordLineEdit->text().isEmpty()) {
        _ui->passwordLineEdit->setFocus();
    } else {
        _ui->infoLabel->setVisible(true);
        _ui->errorLabel->setVisible(false);

        _ui->loginArea->setDisabled(true);
        AccountManager::getInstance().requestAccessToken(_ui->emailLineEdit->text(), _ui->passwordLineEdit->text());
    }
};

void LoginDialog::moveEvent(QMoveEvent* event) {
    // Modal dialogs seemed to get repositioned automatically.  Combat this by moving the window if needed.
    resizeAndPosition();
};


void LoginDialog::toggleQAction() {
    AccountManager& accountManager = AccountManager::getInstance();
    QAction* loginAction = Menu::getInstance()->getActionForOption(MenuOption::Login);
    Q_CHECK_PTR(loginAction);
    
    disconnect(loginAction, 0, 0, 0);
    
    if (accountManager.isLoggedIn()) {
        // change the menu item to logout
        loginAction->setText("Logout " + accountManager.getAccountInfo().getUsername());
        connect(loginAction, &QAction::triggered, &accountManager, &AccountManager::logout);
    } else {
        // change the menu item to login
        loginAction->setText("Login");
        connect(loginAction, &QAction::triggered, this, &LoginDialog::showLoginForCurrentDomain);
    }
}

void LoginDialog::showLoginForCurrentDomain() {
    show();
    resizeAndPosition(false);
}
