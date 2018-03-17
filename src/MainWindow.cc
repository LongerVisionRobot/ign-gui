/*
 * Copyright (C) 2017 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <tinyxml2.h>
#include <string>

#include <ignition/common/Console.hh>
#include <ignition/common/Filesystem.hh>
#include "ignition/gui/Iface.hh"
#include "ignition/gui/MainWindow.hh"
#include "ignition/gui/Plugin.hh"
#include "ignition/gui/qt.h"

namespace ignition
{
  namespace gui
  {
    class MainWindowPrivate
    {
      /// \brief Configuration for this window.
      public: WindowConfig windowConfig;
    };
  }
}

using namespace ignition;
using namespace gui;

/// \brief Strip last component from a path.
/// \return Original path without its last component.
/// \ToDo: Move this function to ignition::common::Filesystem
std::string dirName(const std::string &_path)
{
  std::size_t found = _path.find_last_of("/\\");
  return _path.substr(0, found);
}

/////////////////////////////////////////////////
MainWindow::MainWindow()
  : dataPtr(new MainWindowPrivate)
{
  this->setObjectName("mainWindow");

  // Ubuntu Xenial + Unity: the native menubar is not registering shortcuts,
  // so we register the shortcuts independently of actions
  std::vector<QShortcut *> shortcuts;

  // Title
  std::string title = "Ignition GUI";
  this->setWindowIconText(tr(title.c_str()));
  this->setWindowTitle(tr(title.c_str()));

  // File menu
  auto fileMenu = this->menuBar()->addMenu(tr("&File"));
  fileMenu->setObjectName("fileMenu");

  auto loadConfigAct = new QAction(tr("&Load configuration"), this);
  loadConfigAct->setStatusTip(tr("Load configuration"));
  this->connect(loadConfigAct, SIGNAL(triggered()), this, SLOT(OnLoadConfig()));
  fileMenu->addAction(loadConfigAct);
  shortcuts.push_back(new QShortcut(Qt::CTRL + Qt::Key_O, this,
      SLOT(OnLoadConfig())));

  auto saveConfigAct = new QAction(tr("&Save configuration"), this);
  saveConfigAct->setStatusTip(tr("Save configuration"));
  this->connect(saveConfigAct, SIGNAL(triggered()), this, SLOT(OnSaveConfig()));
  fileMenu->addAction(saveConfigAct);
  shortcuts.push_back(new QShortcut(Qt::CTRL + Qt::Key_S, this,
      SLOT(OnSaveConfig())));

  auto saveConfigAsAct = new QAction(tr("Save configuration as"), this);
  saveConfigAsAct->setStatusTip(tr("Save configuration as"));
  this->connect(saveConfigAsAct, SIGNAL(triggered()), this,
    SLOT(OnSaveConfigAs()));
  fileMenu->addAction(saveConfigAsAct);
  shortcuts.push_back(new QShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_S, this,
      SLOT(OnSaveConfigAs())));

  fileMenu->addSeparator();

  auto loadStylesheetAct = new QAction(tr("&Load stylesheet"), this);
  loadStylesheetAct->setStatusTip(tr("Choose a QSS file to load"));
  this->connect(loadStylesheetAct, SIGNAL(triggered()), this,
      SLOT(OnLoadStylesheet()));
  fileMenu->addAction(loadStylesheetAct);

  fileMenu->addSeparator();

  auto quitAct = new QAction(tr("&Quit"), this);
  quitAct->setStatusTip(tr("Quit"));
  this->connect(quitAct, SIGNAL(triggered()), this, SLOT(close()));
  fileMenu->addAction(quitAct);
  shortcuts.push_back(new QShortcut(Qt::CTRL + Qt::Key_Q, this, SLOT(close())));

  // Plugins menu
  auto pluginsMenu = this->menuBar()->addMenu(tr("&Plugins"));
  pluginsMenu->setObjectName("pluginsMenu");

  auto pluginMapper = new QSignalMapper(this);
  this->connect(pluginMapper, SIGNAL(mapped(QString)),
      this, SLOT(OnAddPlugin(QString)));

  auto plugins = getPluginList();
  for (auto const &path : plugins)
  {
    for (auto const &plugin : path.second)
    {
      auto pluginName = plugin.substr(3, plugin.find(".") - 3);

      auto act = new QAction(QString::fromStdString(pluginName), this);
      this->connect(act, SIGNAL(triggered()), pluginMapper, SLOT(map()));
      pluginMapper->setMapping(act, QString::fromStdString(plugin));
      pluginsMenu->addAction(act);
    }
  }

  // Docking
  this->setDockOptions(QMainWindow::AnimatedDocks |
                       QMainWindow::AllowTabbedDocks |
                       QMainWindow::AllowNestedDocks);
}

/////////////////////////////////////////////////
MainWindow::~MainWindow()
{
}

/////////////////////////////////////////////////
bool MainWindow::CloseAllDocks()
{
  igndbg << "Closing all docks" << std::endl;

  auto docks = this->findChildren<QDockWidget *>();
  for (auto dock : docks)
  {
    dock->close();
    dock->setParent(new QWidget());
  }

  QCoreApplication::processEvents();

  return true;
}

/////////////////////////////////////////////////
void MainWindow::OnLoadConfig()
{
  QFileDialog fileDialog(this, tr("Load configuration"), QDir::homePath(),
      tr("*.config"));
  fileDialog.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint |
      Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);

  if (fileDialog.exec() != QDialog::Accepted)
    return;

  auto selected = fileDialog.selectedFiles();
  if (selected.empty())
    return;

  if (!loadConfig(selected[0].toStdString()))
    return;

  if (!this->CloseAllDocks())
    return;

  addPluginsToWindow();
  applyConfig();
}

/////////////////////////////////////////////////
void MainWindow::OnSaveConfig()
{
  this->SaveConfig(defaultConfigPath());
}

/////////////////////////////////////////////////
void MainWindow::OnSaveConfigAs()
{
  QFileDialog fileDialog(this, tr("Save configuration"), QDir::homePath(),
      tr("*.config"));
  fileDialog.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint |
      Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);
  fileDialog.setAcceptMode(QFileDialog::AcceptSave);

  if (fileDialog.exec() != QDialog::Accepted)
    return;

  auto selected = fileDialog.selectedFiles();
  if (selected.empty())
    return;

  this->SaveConfig(selected[0].toStdString());
}

/////////////////////////////////////////////////
void MainWindow::SaveConfig(const std::string &_path)
{
  std::string config = "<?xml version=\"1.0\"?>\n\n";

  // Window settings
  this->UpdateWindowConfig();
  config += this->dataPtr->windowConfig.XMLString();

  // Plugins
  auto plugins = this->findChildren<Plugin *>();
  for (const auto plugin : plugins)
    config += plugin->ConfigStr();

  // Create the intermediate directories if needed.
  // We check for errors when we try to open the file.
  auto dirname = dirName(_path);
  ignition::common::createDirectories(dirname);

  // Open the file
  std::ofstream out(_path.c_str(), std::ios::out);
  if (!out)
  {
    QMessageBox msgBox;
    std::string str = "Unable to open file: " + _path;
    str += ".\nCheck file permissions.";
    msgBox.setText(str.c_str());
    msgBox.exec();
  }
  else
    out << config;

  ignmsg << "Saved configuration [" << _path << "]" << std::endl;
}

/////////////////////////////////////////////////
void MainWindow::OnLoadStylesheet()
{
  QFileDialog fileDialog(this, tr("Load stylesheet"), QDir::homePath(),
      tr("*.qss"));
  fileDialog.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint |
      Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);

  if (fileDialog.exec() != QDialog::Accepted)
    return;

  auto selected = fileDialog.selectedFiles();
  if (selected.empty())
    return;

  setStyleFromFile(selected[0].toStdString());
}

/////////////////////////////////////////////////
void MainWindow::OnAddPlugin(QString _plugin)
{
  auto plugin = _plugin.toStdString();
  ignlog << "Add [" << plugin << "] via menu" << std::endl;

  loadPlugin(plugin);
  addPluginsToWindow();
}

/////////////////////////////////////////////////
bool MainWindow::ApplyConfig(const WindowConfig &_config)
{
  // Window position
  if (_config.posX >= 0 && _config.posY >= 0)
    this->move(_config.posX, _config.posY);

  // Window size
  if (_config.width >= 0 && _config.height >= 0)
    this->resize(_config.width, _config.height);

  // Docks state
  if (!_config.state.isEmpty())
  {
    if (!this->restoreState(_config.state))
      ignwarn << "Failed to restore state" << std::endl;
  }

  // Stylesheet
  setStyleFromString(_config.styleSheet);

  // Hide menus
  for (auto visible : _config.menuVisibilityMap)
  {
    if (auto menu = this->findChild<QMenu *>(
        QString::fromStdString(visible.first + "Menu")))
    {
      menu->menuAction()->setVisible(visible.second);
    }
  }

  // Plugins menu
  if (auto menu = this->findChild<QMenu *>("pluginsMenu"))
  {
    for (auto action : menu->actions())
    {
      action->setVisible(_config.pluginsFromPaths ||
          std::find(_config.showPlugins.begin(),
                    _config.showPlugins.end(),
                    action->text().toStdString()) !=
                    _config.showPlugins.end());
    }

    for (auto plugin : _config.showPlugins)
    {
      bool exists = false;
      for (auto action : menu->actions())
      {
        if (action->text().toStdString() == plugin)
        {
          exists = true;
          break;
        }
      }

      if (!exists)
      {
        ignwarn << "Requested to show plugin [" << plugin <<
            "] but it doesn't exist." << std::endl;
      }
    }
  }

  // Keep a copy
  this->dataPtr->windowConfig = _config;

  QCoreApplication::processEvents();

  return true;
}

/////////////////////////////////////////////////
void MainWindow::UpdateWindowConfig()
{
  // Position
  this->dataPtr->windowConfig.posX = this->pos().x();
  this->dataPtr->windowConfig.posY = this->pos().y();

  // Size
  this->dataPtr->windowConfig.width = this->width();
  this->dataPtr->windowConfig.height = this->height();

  // Docks state
  this->dataPtr->windowConfig.state = this->saveState();

  // Stylesheet
  this->dataPtr->windowConfig.styleSheet = static_cast<QApplication *>(
      QApplication::instance())->styleSheet().toStdString();

  // Menus configuration is kept the same as the initial one. The menus might
  // have been changed programatically but we don't guarantee that will be
  // saved.
}

/////////////////////////////////////////////////
bool WindowConfig::MergeFromXML(const std::string &_windowXml)
{
  // TinyXml element from string
  tinyxml2::XMLDocument doc;
  doc.Parse(_windowXml.c_str());
  auto winElem = doc.FirstChildElement("window");

  if (!winElem)
    return false;

  // Position
  if (auto xElem = winElem->FirstChildElement("position_x"))
    xElem->QueryIntText(&this->posX);

  if (auto yElem = winElem->FirstChildElement("position_y"))
    yElem->QueryIntText(&this->posY);

  // Size
  if (auto widthElem = winElem->FirstChildElement("width"))
    widthElem->QueryIntText(&this->width);

  if (auto heightElem = winElem->FirstChildElement("height"))
    heightElem->QueryIntText(&this->height);

  // Save on exit
  if (auto saveOnExitElem = winElem->FirstChildElement("save_on_exit"))
  {
    saveOnExitElem->QueryBoolText(&this->saveOnExit);
    std::cout << "Found <save_on_exit>: " << std::boolalpha
              << this->saveOnExit << std::endl;
  }
  else
    std::cout << "Didn't find <save_on_exit>" << std::endl;

  // Docks state
  if (auto stateElem = winElem->FirstChildElement("state"))
  {
    auto text = stateElem->GetText();
    this->state = QByteArray::fromBase64(text);
  }

  // Stylesheet
  if (auto styleElem = winElem->FirstChildElement("stylesheet"))
  {
    if (auto txt = styleElem->GetText())
    {
      setStyleFromString(txt);
    }
    // empty string
    else
    {
      setStyleFromString("");
    }
  }

  // Menus
  if (auto menusElem = winElem->FirstChildElement("menus"))
  {
    // File
    if (auto fileElem = menusElem->FirstChildElement("file"))
    {
      // Visible
      if (fileElem->Attribute("visible"))
      {
        bool visible = true;
        fileElem->QueryBoolAttribute("visible", &visible);
        this->menuVisibilityMap["file"] = visible;
      }
    }

    // Plugins
    if (auto pluginsElem = menusElem->FirstChildElement("plugins"))
    {
      // Visible
      if (pluginsElem->Attribute("visible"))
      {
        bool visible = true;
        pluginsElem->QueryBoolAttribute("visible", &visible);
        this->menuVisibilityMap["plugins"] = visible;
      }

      // From paths
      if (pluginsElem->Attribute("from_paths"))
      {
        bool fromPaths = false;
        pluginsElem->QueryBoolAttribute("from_paths", &fromPaths);
        this->pluginsFromPaths = fromPaths;
      }

      // Show individual plugins
      for (auto showElem = pluginsElem->FirstChildElement("show");
          showElem != nullptr;
          showElem = showElem->NextSiblingElement("show"))
      {
        if (auto pluginName = showElem->GetText())
          this->showPlugins.push_back(pluginName);
      }
    }
  }

  return true;
}

/////////////////////////////////////////////////
std::string WindowConfig::XMLString() const
{
  tinyxml2::XMLDocument doc;

  // Window
  auto windowElem = doc.NewElement("window");
  doc.InsertEndChild(windowElem);

  // Position
  {
    auto elem = doc.NewElement("position_x");
    elem->SetText(std::to_string(this->posX).c_str());
    windowElem->InsertEndChild(elem);
  }

  {
    auto elem = doc.NewElement("position_y");
    elem->SetText(std::to_string(this->posY).c_str());
    windowElem->InsertEndChild(elem);
  }

  // Docks state
  {
    auto elem = doc.NewElement("state");
    elem->SetText(this->state.toBase64().toStdString().c_str());
    windowElem->InsertEndChild(elem);
  }

  // Size
  {
    auto elem = doc.NewElement("width");
    elem->SetText(std::to_string(this->width).c_str());
    windowElem->InsertEndChild(elem);
  }

  {
    auto elem = doc.NewElement("height");
    elem->SetText(std::to_string(this->height).c_str());
    windowElem->InsertEndChild(elem);
  }

  // Save on exit
  {
    auto elem = doc.NewElement("save_on_exit");
    std::ostringstream is;
    is << std::boolalpha << this->saveOnExit;
    elem->SetText(is.str().c_str());
    windowElem->InsertEndChild(elem);
  }

  // Stylesheet
  {
    auto elem = doc.NewElement("stylesheet");
    elem->SetText(this->styleSheet.c_str());
    windowElem->InsertEndChild(elem);
  }

  // Menus
  {
    auto menusElem = doc.NewElement("menus");
    windowElem->InsertEndChild(menusElem);

    // File
    {
      auto elem = doc.NewElement("file");

      // Visible
      auto m = this->menuVisibilityMap.find("file");
      if (m != this->menuVisibilityMap.end())
      {
        elem->SetAttribute("visible", m->second);
      }
      menusElem->InsertEndChild(elem);
    }

    // Plugins
    {
      auto elem = doc.NewElement("plugins");

      // Visible
      auto m = this->menuVisibilityMap.find("plugins");
      if (m != this->menuVisibilityMap.end())
      {
        elem->SetAttribute("visible", m->second);
      }

      // From paths
      elem->SetAttribute("from_paths", this->pluginsFromPaths);

      // Show
      for (const auto &show : this->showPlugins)
      {
        auto showElem = doc.NewElement("show");
        showElem->SetText(show.c_str());
        elem->InsertEndChild(showElem);
      }

      menusElem->InsertEndChild(elem);
    }

    windowElem->InsertEndChild(menusElem);
  }

  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);

  return printer.CStr();
}

//#include <QCloseEvent>
//void MainWindow::closeEvent (QCloseEvent *event)
//{
//    //QMessageBox::StandardButton resBtn = QMessageBox::question( this, "APP_NAME",
//    //                                                            tr("Are you sure?\n"),
//    //                                                            QMessageBox::Cancel | QMessageBox::No | QMessageBox::Yes,
//    //                                                            QMessageBox::Yes);
//    //if (resBtn != QMessageBox::Yes) {
//    //    event->ignore();
//    //} else {
//    //    event->accept();
//    //}
//  std::cout << "Close" << std::endl;
//  this->Close();
//}

//bool MainWindow::CheckForChanges()
//{
//  bool res = true;
//  std::string tmpFilePath = defaultConfigPath() + ".swp";
//
//  this->SaveConfig(tmpFilePath);
//
//  {
//    std::ifstream in(defaultConfigPath());
//    std::ifstream in2(tmpFilePath);
//    while ((!in.eof()) && (!in2.eof()))
//    {
//      std::string line, line2;
//      std::getline(in, line);
//      std::getline(in2, line2);
//      if (line != line2)
//      {
//        res = false;
//        break;
//      }
//    }
//
//    res = res && in.eof() && in2.eof();
//  }
//
//  ignition::common::removeFile(tmpFilePath);
//  return res;
//}
//
//void MainWindow::Close()
//{
//  if (!this->CheckForChanges())
//  {
//    QString msg("Save Changes before exiting?\n\n");
//    QMessageBox msgBox(QMessageBox::NoIcon, QString("Exit"), msg);
//    QPushButton *cancelButton = msgBox.addButton("Cancel",
//        QMessageBox::RejectRole);
//    msgBox.addButton("Don't Save, Exit", QMessageBox::DestructiveRole);
//    QPushButton *saveButton = msgBox.addButton("Save and Exit",
//        QMessageBox::AcceptRole);
//    msgBox.setDefaultButton(cancelButton);
//    msgBox.setDefaultButton(saveButton);
//
//    msgBox.exec();
//    if (msgBox.clickedButton() == cancelButton)
//      return;
//
//    if (msgBox.clickedButton() == saveButton)
//      this->OnSaveConfig();
//  }
//
//  this->close();
//}
