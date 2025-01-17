﻿//  Copyright (c) 2016 - 2020, Marcin Drob

//  Kainote is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  Kainote is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with Kainote.  If not, see <http://www.gnu.org/licenses/>.

#include "TLDialog.h"
#include "KaiStaticBoxSizer.h"
#include "KaiStaticText.h"

TLDialog::TLDialog(wxWindow *parent, SubsGrid *subsgrid)
	: KaiDialog(parent, -1, _("Opcje dopasowywania tłumaczenia"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
	Sbsgrid = subsgrid;
	DialogSizer *sizer = new DialogSizer(wxVERTICAL);
	wxBoxSizer *sizer2 = new wxBoxSizer(wxHORIZONTAL);
	wxGridSizer *sizer1 = new wxGridSizer(2, 2, 2);
	//uwaga nazewnictwo tutaj jest totalnie fuckuped patrz na opisy co dany efekt robi.
	Up = new MappedButton(this, 29995, _("Usuń linię"));
	Up->SetToolTip(_("Usuwa zaznaczoną linijkę.\nTłumaczenie idzie do góry."));
	Down = new MappedButton(this, 29997, _("Dodaj linię"));
	Down->SetToolTip(_("Dodaje pustą linijkę przed zaznaczoną.\nTłumaczenie idzie w dół."));
	UpJoin = new MappedButton(this, 29998, _("Złącz linie"));
	UpJoin->SetToolTip(_("Złącza następną linijkę z zaznaczoną.\nTłumaczenie idzie do góry."));
	DownJoin = new MappedButton(this, 29996, _("Złącz linie"));
	DownJoin->SetToolTip(_("Złącza następną linijkę z zaznaczoną.\nOryginał idzie w górę."));
	DownDel = new MappedButton(this, 29994, _("Usuń linię"));
	DownDel->SetToolTip(_("Usuwa zaznaczoną linijkę.\nOryginał idzie do góry."));
	UpExt = new MappedButton(this, 29993, _("Dodaj linię"));
	UpExt->SetToolTip(_("Dodaje pustą linijkę przed zaznaczoną.\nOryginał idzie w dół.\nDodanej linii należy ustawić czasy."));

	sizer2->Add(new KaiStaticText(this, -1, _("Tekst oryginału")), 1, wxLEFT | wxRIGHT | wxEXPAND | wxALIGN_CENTER_VERTICAL, 5);
	sizer2->Add(new KaiStaticText(this, -1, _("Tekst tłumaczenia")), 1, wxLEFT | wxRIGHT | wxEXPAND | wxALIGN_CENTER_VERTICAL, 5);

	sizer1->Add(UpExt, 0, wxALL | wxEXPAND, 5);
	sizer1->Add(Down, 0, wxALL | wxEXPAND, 5);
	sizer1->Add(DownJoin, 0, wxALL | wxEXPAND, 5);
	sizer1->Add(UpJoin, 0, wxALL | wxEXPAND, 5);
	sizer1->Add(DownDel, 0, wxALL | wxEXPAND, 5);
	sizer1->Add(Up, 0, wxALL | wxEXPAND, 5);

	sizer->Add(sizer2, 0, wxEXPAND | wxTOP, 5);
	sizer->Add(sizer1, 0, wxEXPAND);
	sizer->Add(new KaiStaticText(this, -1, _("Objaśnienie:\nOryginał - tekst napisów z właściwym timingiem służy\ndo porównania wklejanych dialogów, później zostaje usunięty.\nTłumaczenie - tekst wklejony do napisów z poprawnym timingiem.")), 0, wxEXPAND | wxALL, 5);
	SetSizerAndFit(sizer);
	CenterOnParent();

	Connect(29997, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnDown);
	Connect(29998, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnUpJoin);
	Connect(29995, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnUp);
	Connect(29996, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnDownJoin);
	Connect(29993, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnUpExt);
	Connect(29994, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&TLDialog::OnDownDel);
}

TLDialog::~TLDialog()
{

}

void TLDialog::OnDown(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(3);
}

void TLDialog::OnUp(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(0);
}

void TLDialog::OnUpJoin(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(1);
}

void TLDialog::OnDownJoin(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(4);
}

void TLDialog::OnUpExt(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(2);
}

void TLDialog::OnDownDel(wxCommandEvent& event)
{
	Sbsgrid->MoveTextTL(5);
}