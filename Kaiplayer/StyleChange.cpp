﻿//  Copyright (c) 2016, Marcin Drob

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


#include <wx/wx.h>
#include "StyleChange.h"
#include <wx/settings.h>
#include "FontEnumerator.h"
#include "config.h" 
#include "ColorPicker.h"
#include "KaiStaticBoxSizer.h"


wxColour Blackorwhite(wxColour kol)
{
	int result = (kol.Red() > 127) + (kol.Green() > 127) + (kol.Blue() > 127);
	int kols = (result < 2) ? 255 : 0;
	return wxColour(kols, kols, kols);
}

StyleChange::StyleChange(wxWindow* parent, bool window, const wxPoint& pos)
//: wxWindow(parent,-1,pos)
:SCD(NULL)
, SS((StyleStore*)parent)
{
	SetForegroundColour(Options.GetColour(WindowText));
	SetBackgroundColour(Options.GetColour(WindowBackground));
	DialogSizer *ds = NULL;
	wxBoxSizer *Main1 = NULL;
	wxBoxSizer *Main2 = NULL;
	wxBoxSizer *Main3 = NULL;
	if (!window){
		SCD = new KaiDialog(parent->GetParent(), -1, _("Edycja Stylu"), pos, wxDefaultSize, wxRESIZE_BORDER);
		Create(SCD, -1);
		ds = new DialogSizer(wxHORIZONTAL);
		Main1 = new wxBoxSizer(wxVERTICAL);
		Main2 = new wxBoxSizer(wxVERTICAL);
		Main3 = new wxBoxSizer(wxHORIZONTAL);
	}
	else{
		Create(parent, -1);
	}
	Preview = NULL;
	updateStyle = NULL;
	block = true;
	wxFont font(8, wxSWISS, wxFONTSTYLE_NORMAL, wxNORMAL, false, "Tahoma", wxFONTENCODING_DEFAULT);
	SetFont(font);

	wxBoxSizer *Main = new wxBoxSizer(wxVERTICAL);

	KaiStaticBoxSizer *stylename = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Nazwa stylu:"));
	wxTextValidator valid(wxFILTER_EXCLUDE_CHAR_LIST);
	valid.SetCharExcludes(",");
	styleName = new KaiTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER, valid);
	styleName->SetMaxLength(500);
	stylename->Add(styleName, 1, wxEXPAND | wxALL, 2);

	KaiStaticBoxSizer *stylefont = new KaiStaticBoxSizer(wxVERTICAL, this, _("Czcionka i rozmiar:"));
	wxBoxSizer *fntsizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *filtersizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *biussizer = new wxBoxSizer(wxHORIZONTAL);
	const wxString & fontFilterText = Options.GetString(StyleEditFilterText);
	bool fontFilterOn = Options.GetBool(StyleFilterTextOn);
	styleFont = new KaiChoice(this, ID_FONTNAME, "", wxDefaultPosition, wxDefaultSize, wxArrayString());
	if (fontFilterText.IsEmpty() || !fontFilterOn){
		styleFont->PutArray(FontEnum.GetFonts(this, [=](){
			SS->ReloadFonts();
		}));
	}
	else{
		styleFont->PutArray(FontEnum.GetFilteredFonts(this, [=](){
			SS->ReloadFonts();
		}, fontFilterText));
	}
	fontSize = new NumCtrl(this, ID_TOUTLINE, "32", 1, 10000, false, wxDefaultPosition, wxSize(66, -1), wxTE_PROCESS_ENTER);
	fontFilter = new KaiTextCtrl(this, -1, fontFilterText);
	Filter = new ToggleButton(this, 21342, _("Filtruj"));
	Filter->SetToolTip(_("Filtruje czcionki, by zawierały wpisane znaki"));
	Filter->SetValue(fontFilterOn);
	Bind(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, [=](wxCommandEvent &evt){
		wxString filter = fontFilter->GetValue();
		bool filterOn = Filter->GetValue();
		if (filter.IsEmpty() || !filterOn){
			styleFont->PutArray(FontEnum.GetFonts(this, [=](){
				SS->ReloadFonts();
			}));
			FontEnum.RemoveFilteredClient(this);
		}
		else{
			styleFont->PutArray(FontEnum.GetFilteredFonts(this, [=](){
				SS->ReloadFonts();
			}, filter));
		}
		Options.SetString(StyleEditFilterText, filter);
		Options.SetBool(StyleFilterTextOn, filterOn);
	}, 21342);

	textBold = new KaiCheckBox(this, ID_CBOLD, _("Pogrubienie")/*, wxDefaultPosition, wxSize(73, 15)*/);
	textItalic = new KaiCheckBox(this, ID_CBOLD, _("Kursywa")/*, wxDefaultPosition, wxSize(73, 15)*/);
	textUnderline = new KaiCheckBox(this, ID_CBOLD, _("Podkreślenie")/*, wxDefaultPosition, wxSize(73, 15)*/);
	textStrikeout = new KaiCheckBox(this, ID_CBOLD, _("Przekreślenie")/*, wxDefaultPosition, wxSize(73, 15)*/);

	fntsizer->Add(styleFont, 3, wxEXPAND | wxALL, 2);
	fntsizer->Add(fontSize, 1, wxEXPAND | wxALL, 2);
	filtersizer->Add(fontFilter, 3, wxEXPAND | wxALL, 2);
	filtersizer->Add(Filter, 1, wxEXPAND | wxALL, 2);

	biussizer->Add(textBold, 1, wxEXPAND | wxALL, 2);
	biussizer->Add(textItalic, 1, wxEXPAND | wxALL, 2);
	biussizer->Add(textUnderline, 1, wxEXPAND | wxALL, 2);
	biussizer->Add(textStrikeout, 1, wxEXPAND | wxALL, 2);

	stylefont->Add(fntsizer, 0, wxEXPAND, 0);
	stylefont->Add(filtersizer, 0, wxEXPAND, 0);
	stylefont->Add(biussizer, 0, wxEXPAND | wxALIGN_CENTER, 0);

	KaiStaticBoxSizer *stylekol = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Kolory i przezroczystość:"));

	wxGridSizer *kolgrid = new wxGridSizer(4, 2, 2);

	color1 = new MappedButton(this, ID_BCOLOR1, _("Pierwszy"));
	color1->Bind(wxEVT_RIGHT_UP, &StyleChange::OnColor1RightClick, this);
	color2 = new MappedButton(this, ID_BCOLOR2, _("Drugi"));
	color2->Bind(wxEVT_RIGHT_UP, &StyleChange::OnColor2RightClick, this);
	color3 = new MappedButton(this, ID_BCOLOR3, _("Obwódka"));
	color3->Bind(wxEVT_RIGHT_UP, &StyleChange::OnColor3RightClick, this);
	color4 = new MappedButton(this, ID_BCOLOR4, _("Cień"));
	color4->Bind(wxEVT_RIGHT_UP, &StyleChange::OnColor4RightClick, this);

	alpha1 = new NumCtrl(this, ID_TOUTLINE, "0", 0, 255, true, wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);
	alpha2 = new NumCtrl(this, ID_TOUTLINE, "0", 0, 255, true, wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);
	alpha3 = new NumCtrl(this, ID_TOUTLINE, "0", 0, 255, true, wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);
	alpha4 = new NumCtrl(this, ID_TOUTLINE, "0", 0, 255, true, wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);

	kolgrid->Add(color1, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(color2, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(color3, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(color4, 1, wxEXPAND | wxALL, 2);

	kolgrid->Add(alpha1, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(alpha2, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(alpha3, 1, wxEXPAND | wxALL, 2);
	kolgrid->Add(alpha4, 1, wxEXPAND | wxALL, 2);

	stylekol->Add(kolgrid, 1, wxEXPAND | wxALL, 2);

	KaiStaticBoxSizer *styleattr = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Obwódka:               Cień:                      Skala X:                 Skala Y:"));

	outline = new NumCtrl(this, ID_TOUTLINE, "", 0, 1000, false, wxDefaultPosition, wxSize(83, -1), wxTE_PROCESS_ENTER);
	shadow = new NumCtrl(this, ID_TOUTLINE, "", 0, 1000000, false, wxDefaultPosition, wxSize(83, -1), wxTE_PROCESS_ENTER);
	scaleX = new NumCtrl(this, ID_TOUTLINE, "", 1, 10000000, false, wxDefaultPosition, wxSize(83, -1), wxTE_PROCESS_ENTER);
	scaleY = new NumCtrl(this, ID_TOUTLINE, "", 1, 10000000, false, wxDefaultPosition, wxSize(83, -1), wxTE_PROCESS_ENTER);

	styleattr->Add(outline, 1, wxEXPAND | wxALL, 2);
	styleattr->Add(shadow, 1, wxEXPAND | wxALL, 2);
	styleattr->Add(scaleX, 1, wxEXPAND | wxALL, 2);
	styleattr->Add(scaleY, 1, wxEXPAND | wxALL, 2);

	wxBoxSizer *sizer1 = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer *sizer2 = new wxBoxSizer(wxHORIZONTAL);
	KaiStaticBoxSizer *styleattr1 = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Kąt:                     Odstępy:              Typ obwódki:"));

	angle = new NumCtrl(this, ID_TOUTLINE, "", -1000000, 1000000, false, wxDefaultPosition, wxSize(65, -1), wxTE_PROCESS_ENTER);
	spacing = new NumCtrl(this, ID_TOUTLINE, "", -1000000, 1000000, false, wxDefaultPosition, wxSize(65, -1), wxTE_PROCESS_ENTER);
	borderStyle = new KaiCheckBox(this, ID_CBOLD, _("Prost. obw."));

	styleattr1->Add(angle, 1, wxEXPAND | wxALL, 2);
	styleattr1->Add(spacing, 1, wxEXPAND | wxALL, 2);
	styleattr1->Add(borderStyle, 1, wxEXPAND | wxALL, 2);

	KaiStaticBoxSizer *stylemargs = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Margines lewy:     Prawy:                Pionowy:"));

	leftMargin = new NumCtrl(this, ID_TOUTLINE, "", 0, 9999, true, wxDefaultPosition, wxSize(65, -1), wxTE_PROCESS_ENTER);
	rightMargin = new NumCtrl(this, ID_TOUTLINE, "", 0, 9999, true, wxDefaultPosition, wxSize(65, -1), wxTE_PROCESS_ENTER);
	verticalMargin = new NumCtrl(this, ID_TOUTLINE, "", 0, 9999, true, wxDefaultPosition, wxSize(65, -1), wxTE_PROCESS_ENTER);

	stylemargs->Add(leftMargin, 1, wxEXPAND | wxALL, 2);
	stylemargs->Add(rightMargin, 1, wxEXPAND | wxALL, 2);
	stylemargs->Add(verticalMargin, 1, wxEXPAND | wxALL, 2);

	sizer1->Add(styleattr1, 0, wxEXPAND, 0);
	sizer1->Add(stylemargs, 0, wxEXPAND, 0);

	KaiStaticBoxSizer *stylean = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Położenie tekstu:"));

	wxGridSizer *angrid = new wxGridSizer(3, 5, 2);

	alignment7 = new KaiRadioButton(this, ID_RAN7, "7", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	alignment8 = new KaiRadioButton(this, ID_RAN8, "8");
	alignment9 = new KaiRadioButton(this, ID_RAN9, "9");
	alignment4 = new KaiRadioButton(this, ID_RAN4, "4");
	alignment5 = new KaiRadioButton(this, ID_RAN5, "5");
	alignment6 = new KaiRadioButton(this, ID_RAN6, "6");
	alignment1 = new KaiRadioButton(this, ID_RAN1, "1");
	alignment2 = new KaiRadioButton(this, ID_RAN2, "2");
	alignment3 = new KaiRadioButton(this, ID_RAN3, "3");

	angrid->Add(alignment7, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment8, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment9, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment4, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment5, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment6, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment1, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment2, 1, wxEXPAND | wxALL, 2);
	angrid->Add(alignment3, 1, wxEXPAND | wxALL, 2);

	stylean->Add(angrid, 0, wxEXPAND | wxALL, 2);

	sizer2->Add(sizer1, 0, wxEXPAND, 0);
	sizer2->Add(stylean, 0, wxEXPAND | wxLEFT, 4);


	encs.Add(_("0 - ANSI"));
	encs.Add(_("1 - Domyślny"));
	encs.Add(_("2 - Symbol"));
	encs.Add(_("77 - Mac"));
	encs.Add(_("128 - Japoński"));
	encs.Add(_("129 - Koreański"));
	encs.Add(_("130 - Johab"));
	encs.Add(_("134 - Chiński GB2312"));
	encs.Add(_("135 - Chiński BIG5"));
	encs.Add(_("161 - Grecki"));
	encs.Add(_("162 - Turecki"));
	encs.Add(_("163 - Wietnamski"));
	encs.Add(_("177 - Hebrajski"));
	encs.Add(_("178 - Arabski"));
	encs.Add(_("186 - Języki Bałtyckie"));
	encs.Add(_("204 - Rosyjski"));
	encs.Add(_("222 - Tajski"));
	encs.Add(_("238 - Europa Środkowa (Polski)"));
	encs.Add(_("255 - OEM"));

	KaiStaticBoxSizer *styleenc = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Kodowanie tekstu:"));
	textEncoding = new KaiChoice(this, ID_CENCODING, wxDefaultPosition, wxDefaultSize, encs);
	styleenc->Add(textEncoding, 1, wxEXPAND | wxALL, 2);

	KaiStaticBoxSizer *styleprev = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Podgląd stylu:"));
	Preview = new StylePreview(this, -1, wxDefaultPosition, wxSize(-1, 100));
	styleprev->Add(Preview, 1, wxEXPAND | wxALL, 2);

	wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
	btnOk = new MappedButton(this, ID_BOK, "Ok");
	btnCommit = new MappedButton(this, ID_B_COMMIT, _("Zastosuj"));
	btnCommitOnStyles = new MappedButton(this, ID_B_CHANGE_ALL_SELECTED_STYLES, _("Zastosuj na zaznaczonych"));
	btnCancel = new MappedButton(this, ID_BCANCEL, _("Anuluj"));

	buttons->Add(btnOk, 1, wxEXPAND | wxALL, 2);
	buttons->Add(btnCommit, 1, wxEXPAND | wxALL, 2);
	buttons->Add(btnCommitOnStyles, 0, wxEXPAND | wxALL, 2);
	buttons->Add(btnCancel, 1, wxEXPAND | wxALL, 2);

	//Main sizer
	if (window){
		Main->Add(stylename, 0, wxEXPAND | wxALL, 2);
		Main->Add(stylefont, 0, wxEXPAND | wxALL, 2);
		Main->Add(stylekol, 0, wxEXPAND | wxALL, 2);
		Main->Add(styleattr, 0, wxEXPAND | wxALL, 2);
		Main->Add(sizer2, 0, wxEXPAND | wxALL, 2);
		Main->Add(styleenc, 0, wxEXPAND | wxALL, 2);
		Main->Add(styleprev, 1, wxEXPAND | wxALL, 2);
		Main->Add(buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 4);
		SetSizerAndFit(Main);
	}
	else{
		Main1->Add(stylename, 0, wxEXPAND | wxALL, 2);
		Main1->Add(stylefont, 0, wxEXPAND | wxALL, 2);
		Main1->Add(stylekol, 0, wxEXPAND | wxALL, 2);
		Main2->Add(styleattr, 0, wxEXPAND | wxALL, 2);
		Main2->Add(sizer2, 0, wxEXPAND | wxALL, 2);
		Main2->Add(styleenc, 0, wxEXPAND | wxALL, 2);
		Main3->Add(Main1, 0, wxEXPAND);
		Main3->Add(Main2, 0, wxEXPAND);
		Main->Add(Main3, 0, wxEXPAND);
		Main->Add(styleprev, 1, wxEXPAND | wxALL, 2);
		Main->Add(buttons, 0, wxBOTTOM | wxLEFT | wxRIGHT | wxALIGN_CENTER, 4);
		SetSizerAndFit(Main);
	}

	Connect(ID_BCOLOR1, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnColor1Click);
	Connect(ID_BCOLOR2, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnColor2Click);
	Connect(ID_BCOLOR3, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnColor3Click);
	Connect(ID_BCOLOR4, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnColor4Click);
	Connect(ID_BOK, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnOKClick);
	Connect(ID_BCANCEL, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnCancelClick);
	Connect(ID_B_CHANGE_ALL_SELECTED_STYLES, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnChangeAllSelectedStyles);
	Connect(ID_B_COMMIT, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&StyleChange::OnCommit);
	Connect(ID_FONTNAME, wxEVT_COMMAND_COMBOBOX_SELECTED, (wxObjectEventFunction)&StyleChange::OnUpdatePreview);
	Connect(ID_TOUTLINE, NUMBER_CHANGED, (wxObjectEventFunction)&StyleChange::OnUpdatePreview);
	Connect(ID_CBOLD, wxEVT_COMMAND_CHECKBOX_CLICKED, (wxObjectEventFunction)&StyleChange::OnUpdatePreview);
	Connect(ID_CENCODING, wxEVT_COMMAND_CHOICE_SELECTED, (wxObjectEventFunction)&StyleChange::OnUpdatePreview);
	//Connect(ID_RAN1, ID_RAN9,wxEVT_COMMAND_RADIOBUTTON_SELECTED,(wxObjectEventFunction)&StyleChange::OnUpdatePreview);
	DoTooltips();
	if (ds){
		ds->Add(this, 1, wxEXPAND);
		SCD->SetSizerAndFit(ds);
		SCD->SetEnterId(ID_BOK);
		//SCD->SetEscapeId(ID_BCANCEL);
		SCD->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){
			OnOKClick(evt);
		}, ID_BOK);
	}
	block = false;
}

StyleChange::~StyleChange()
{
	FontEnum.RemoveClient(this);
	wxDELETE(updateStyle);
	wxDELETE(CompareStyle);
}

void StyleChange::OnAllCols(int numColor, bool leftClick /*= true*/)
{
	if (Options.GetBool(COLORPICKER_SWITCH_CLICKS))
		leftClick = !leftClick;

	MappedButton *color = NULL;
	NumCtrl *alpha = NULL;
	GetColorControls(&color, &alpha, numColor);
	wxColour actualColor = color->GetBackgroundColour();
	//this color will replace original colors;
	lastColor = AssColor(actualColor, alpha->GetInt());

	if (leftClick){
		DialogColorPicker *ColourDialog = DialogColorPicker::Get(this, lastColor, numColor);
		int colorDialogId = ColourDialog->GetId();
		MoveToMousePosition(ColourDialog);
		ColourDialog->Bind(COLOR_TYPE_CHANGED, [=](wxCommandEvent &evt){
			MappedButton *colorButton = NULL;
			NumCtrl *alphaButton = NULL;
			GetColorControls(&colorButton, &alphaButton, evt.GetInt());
			lastColor = AssColor(colorButton->GetBackgroundColour(), alphaButton->GetInt());
			ColourDialog->SetColor(lastColor, 0, false);
		}, colorDialogId);
		ColourDialog->Bind(COLOR_CHANGED, [=](ColorEvent &evt){
			UpdateColor(evt.GetColor(), evt.GetColorType());
		}, colorDialogId);

		if (ColourDialog->ShowModal() == wxID_OK) {
			//get color for recent
			ColourDialog->GetColor();
			//looks like when COLOR_CHANGED is bind it's no need to do something in here
			//UpdateColor(ColourDialog->GetColor(), ColourDialog->GetColorType());
		}
		else{
			//set last color after cancel
			UpdateColor(lastColor, ColourDialog->GetColorType());
		}
	}
	else{
		SimpleColorPicker scp(this, actualColor, numColor);
		SimpleColorPickerDialog *scpd = scp.GetDialog();
		int spcdId = scpd->GetId();
		scpd->Bind(COLOR_CHANGED, [=](ColorEvent &evt){
			UpdateColor(evt.GetColor(), evt.GetColorType());
		}, spcdId);
		scpd->Bind(COLOR_TYPE_CHANGED, [=](wxCommandEvent &evt){
			MappedButton *colorButton = NULL;
			NumCtrl *alphaButton = NULL;
			GetColorControls(&colorButton, &alphaButton, evt.GetInt());
			lastColor = AssColor(colorButton->GetBackgroundColour(), alphaButton->GetInt());
			scpd->SetColor(lastColor);
		}, spcdId);

		AssColor ret;
		if (scp.PickColor(&ret)){
			scpd->AddRecent();
			//looks like when COLOR_CHANGED is bind it's no need to do something in here
			//UpdateColor(scpd->GetColor(), scpd->GetColorType());
		}
		else{
			//set last color after cancel
			UpdateColor(lastColor, scpd->GetColorType());
		}
	}
}

void StyleChange::OnColor1Click(wxCommandEvent& event)
{
	OnAllCols(1);
}

void StyleChange::OnColor2Click(wxCommandEvent& event)
{
	OnAllCols(2);
}

void StyleChange::OnColor3Click(wxCommandEvent& event)
{
	OnAllCols(3);
}

void StyleChange::OnColor4Click(wxCommandEvent& event)
{
	OnAllCols(4);
}

void StyleChange::OnColor1RightClick(wxMouseEvent& event)
{
	OnAllCols(1, false);
}

void StyleChange::OnColor2RightClick(wxMouseEvent& event)
{
	OnAllCols(2, false);
}

void StyleChange::OnColor3RightClick(wxMouseEvent& event)
{
	OnAllCols(3, false);
}

void StyleChange::OnColor4RightClick(wxMouseEvent& event)
{
	OnAllCols(4, false);
}

void StyleChange::OnOKClick(wxCommandEvent& event)
{
	UpdateStyle();
	//copied style, to avoid memory leaks release after using. 
	//double enter can crash it
	if (updateStyle && SS->ChangeStyle(updateStyle->Copy())){
		Hide();
		if (SCD){ SCD->Hide(); }
		wxDELETE(updateStyle);
		SS->Mainall->Fit(SS);
	}

}

void StyleChange::OnCancelClick(wxCommandEvent& event)
{
	Hide();
	if (SCD){ SCD->Hide(); }
	wxDELETE(updateStyle);
	SS->Mainall->Fit(SS);
}

void StyleChange::UpdateValues(Styles *style, bool allowMultiEdition, bool enableNow)
{
	block = true;
	wxDELETE(updateStyle);
	updateStyle = style;
	styleName->SetValue(updateStyle->Name);
	int sell = styleFont->FindString(updateStyle->Fontname);
	if (sell == -1){
		styleFont->SetValue(updateStyle->Fontname);
	}
	else{ styleFont->SetSelection(sell); }

	fontSize->SetString(updateStyle->Fontsize);
	wxColour kol = updateStyle->PrimaryColour.GetWX();
	color1->SetBackgroundColour(kol);
	color1->SetForegroundColour(Blackorwhite(kol));
	kol = updateStyle->SecondaryColour.GetWX();
	color2->SetBackgroundColour(kol);
	color2->SetForegroundColour(Blackorwhite(kol));
	kol = updateStyle->OutlineColour.GetWX();
	color3->SetBackgroundColour(kol);
	color3->SetForegroundColour(Blackorwhite(kol));
	kol = updateStyle->BackColour.GetWX();
	color4->SetBackgroundColour(kol);
	color4->SetForegroundColour(Blackorwhite(kol));

	alpha1->SetInt(updateStyle->PrimaryColour.a);
	alpha2->SetInt(updateStyle->SecondaryColour.a);
	alpha3->SetInt(updateStyle->OutlineColour.a);
	alpha4->SetInt(updateStyle->BackColour.a);
	textBold->SetValue(updateStyle->Bold);
	textItalic->SetValue(updateStyle->Italic);
	textUnderline->SetValue(updateStyle->Underline);
	textStrikeout->SetValue(updateStyle->StrikeOut);
	angle->SetString(updateStyle->Angle);
	spacing->SetString(updateStyle->Spacing);
	outline->SetString(updateStyle->Outline);
	shadow->SetString(updateStyle->Shadow);
	borderStyle->SetValue(updateStyle->BorderStyle);
	//if(tab->BorderStyle){sob->SetValue(true);}else{sob->SetValue(false);};
	wxString an = updateStyle->Alignment;
	if (an == "1"){ alignment1->SetValue(true); }
	else if (an == "2"){ alignment2->SetValue(true); }
	else if (an == "3"){ alignment3->SetValue(true); }
	else if (an == "4"){ alignment4->SetValue(true); }
	else if (an == "5"){ alignment5->SetValue(true); }
	else if (an == "6"){ alignment6->SetValue(true); }
	else if (an == "7"){ alignment7->SetValue(true); }
	else if (an == "8"){ alignment8->SetValue(true); }
	else if (an == "9"){ alignment9->SetValue(true); };
	scaleX->SetString(updateStyle->ScaleX);
	scaleY->SetString(updateStyle->ScaleY);
	leftMargin->SetString(updateStyle->MarginL);
	rightMargin->SetString(updateStyle->MarginR);
	verticalMargin->SetString(updateStyle->MarginV);
	int choice = -1;
	for (size_t i = 0; i < encs.size(); i++){
		if (encs[i].StartsWith(updateStyle->Encoding + " ")){ choice = i; break; }
	}
	if (choice == -1){ choice = 1; }
	if (allowMultiEdition != allowMultiEdition){
		btnCommitOnStyles->Enable(allowMultiEdition);
		allowMultiEdition = allowMultiEdition;
	}
	if (allowMultiEdition){
		if (CompareStyle)
			delete CompareStyle;

		CompareStyle = style->Copy();
		if (!enableNow){
			btnCommitOnStyles->Enable(false);
		}
	}
	textEncoding->SetSelection(choice);
	block = false;
	UpdatePreview();
	Show();
}

void StyleChange::OnChangeAllSelectedStyles(wxCommandEvent& event)
{
	if (!updateStyle || !allowMultiEdition || !CompareStyle){
		KaiLog("Style was released or not allowed");
		return;
	}
	UpdateStyle();
	int changes = CompareStyle->Compare(updateStyle);
	if (!changes)
		changes = -1;
	//tab stays not released cause dialog lose its style but is still visible
	SS->ChangeStyle(updateStyle->Copy(), changes);
}

void StyleChange::OnCommit(wxCommandEvent& event)
{
	UpdateStyle();
	//tab stays not released cause dialog lose its style but is still visible
	SS->ChangeStyle(updateStyle->Copy());
}


void StyleChange::UpdateStyle()
{
	if (!updateStyle){ return; }
	updateStyle->Name = styleName->GetValue();
	updateStyle->Fontname = styleFont->GetValue();
	updateStyle->Fontsize = fontSize->GetString();
	updateStyle->PrimaryColour.SetWX(color1->GetBackgroundColour(), alpha1->GetInt());
	updateStyle->SecondaryColour.SetWX(color2->GetBackgroundColour(), alpha2->GetInt());
	updateStyle->OutlineColour.SetWX(color3->GetBackgroundColour(), alpha3->GetInt());
	updateStyle->BackColour.SetWX(color4->GetBackgroundColour(), alpha4->GetInt());
	updateStyle->Bold = textBold->GetValue();
	updateStyle->Italic = textItalic->GetValue();
	updateStyle->Underline = textUnderline->GetValue();
	updateStyle->StrikeOut = textStrikeout->GetValue();
	updateStyle->Angle = angle->GetString();
	updateStyle->Spacing = spacing->GetString();
	updateStyle->Outline = outline->GetString();
	updateStyle->Shadow = shadow->GetString();
	updateStyle->BorderStyle = borderStyle->GetValue();
	wxString an;
	if (alignment1->GetValue()){ an = "1"; }
	else if (alignment2->GetValue()){ an = "2"; }
	else if (alignment3->GetValue()){ an = "3"; }
	else if (alignment4->GetValue()){ an = "4"; }
	else if (alignment5->GetValue()){ an = "5"; }
	else if (alignment6->GetValue()){ an = "6"; }
	else if (alignment7->GetValue()){ an = "7"; }
	else if (alignment8->GetValue()){ an = "8"; }
	else if (alignment9->GetValue()){ an = "9"; };
	updateStyle->Alignment = an;
	updateStyle->ScaleX = scaleX->GetString();
	updateStyle->ScaleY = scaleY->GetString();
	updateStyle->MarginL = leftMargin->GetString();
	updateStyle->MarginR = rightMargin->GetString();
	updateStyle->MarginV = verticalMargin->GetString();
	updateStyle->Encoding = textEncoding->GetString(textEncoding->GetSelection()).BeforeFirst(L' ');
}

void StyleChange::UpdatePreview()
{
	if (!Preview)
		return;
	UpdateStyle();
	Preview->DrawPreview(updateStyle);
}

void StyleChange::OnUpdatePreview(wxCommandEvent& event)
{
	if (!block) {
		UpdatePreview();
	}
}

bool StyleChange::Show(bool show)
{
	wxWindow::Show(show);
	if (SCD){
		if (show && !SCD->IsShown()){ MoveToMousePosition(SCD); }
		SCD->Show(show);
	}
	return true;// wxWindow::Show(show);
}

void StyleChange::DoTooltips()
{
	styleName->SetToolTip(_("Nazwa stylu"));
	styleFont->SetToolTip(_("Czcionka"));
	fontSize->SetToolTip(_("Rozmiar czcionki"));
	textBold->SetToolTip(_("Pogrubienie"));
	textItalic->SetToolTip(_("Pochylenie"));
	textUnderline->SetToolTip(_("Podkreślenie"));
	textStrikeout->SetToolTip(_("Przekreślenie"));
	color1->SetToolTip(_("Kolor podstawowy"));
	color2->SetToolTip(_("Kolor zastępczy do karaoke"));
	color3->SetToolTip(_("Kolor obwódki"));
	color4->SetToolTip(_("Kolor cienia"));
	alpha1->SetToolTip(_("Przezroczystość koloru podstawowego, 0 - brak, 255 - przezroczystość"));
	alpha2->SetToolTip(_("Przezroczystość koloru zastępczego, 0 - brak, 255 - przezroczystość"));
	alpha3->SetToolTip(_("Przezroczystość koloru obwódki, 0 - brak, 255 - przezroczystość"));
	alpha4->SetToolTip(_("Przezroczystość koloru cienia, 0 - brak, 255 - przezroczystość"));
	outline->SetToolTip(_("Obwódka w pikselach"));
	shadow->SetToolTip(_("Cień w pikselach"));
	scaleX->SetToolTip(_("Skala X w procentach"));
	scaleY->SetToolTip(_("Skala Y w procentach"));
	angle->SetToolTip(_("Kąt w stopniach"));
	spacing->SetToolTip(_("Odstępy między literami w pikselach (wartości ujemne dozwolone)"));
	borderStyle->SetToolTip(_("Prostokątna obwódka"));
	rightMargin->SetToolTip(_("Margines prawy"));
	leftMargin->SetToolTip(_("Margines lewy"));
	verticalMargin->SetToolTip(_("Margines górny i dolny"));
	alignment1->SetToolTip(_("Lewy dolny róg"));
	alignment2->SetToolTip(_("Wyśrodkowane na dole"));
	alignment3->SetToolTip(_("Prawy dolny róg"));
	alignment4->SetToolTip(_("Wyśrodkowane po lewej"));
	alignment5->SetToolTip(_("Wyśrodkowane"));
	alignment6->SetToolTip(_("Wyśrodkowane po prawej"));
	alignment7->SetToolTip(_("Lewy górny róg"));
	alignment8->SetToolTip(_("Wyśrodkowane u góry"));
	alignment9->SetToolTip(_("Prawy górny róg"));
	textEncoding->SetToolTip(_("Kodowanie tekstu"));
}

void StyleChange::GetColorControls(MappedButton** color, NumCtrl** alpha, int numColor)
{
	if(color)
		*color = (numColor == 1) ? color1 : (numColor == 2) ? color2 : (numColor == 3) ? color3 : color4;
	if (alpha)
		*alpha = (numColor == 1) ? alpha1 : (numColor == 2) ? alpha2 : (numColor == 3) ? alpha3 : alpha4;
}

void StyleChange::UpdateColor(const AssColor &pickedColor, int numColor)
{
	MappedButton *color = NULL;
	NumCtrl *alpha = NULL;
	GetColorControls(&color, &alpha, numColor);
	color->SetForegroundColour(Blackorwhite(pickedColor.GetWX()));
	color->SetBackgroundColour(pickedColor.GetWX());
	alpha->SetInt(pickedColor.a);
	UpdatePreview();
}

bool StyleChange::Destroy()
{
	if (SCD){ return SCD->Destroy(); }
	else{ return wxWindowBase::Destroy(); }
}
bool StyleChange::IsShown()
{
	if (SCD){ return SCD->IsShown(); }
	else{ return wxWindow::IsShown(); }
}