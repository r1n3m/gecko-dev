/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "nsTextWidget.h"
#include <ToolUtils.h>
#include <Appearance.h>

NS_IMPL_ADDREF(nsTextWidget);
NS_IMPL_RELEASE(nsTextWidget);


//-------------------------------------------------------------------------
//	� NOTE ABOUT MENU HANDLING �
//
//	The definitions below, as well as the NS_MENU_SELECTED code in
//	DispatchEvent() are temporary hacks only. They require the menu
//	resources to be created under Contructor with the standard
//	PowerPlant menu IDs. All that will go away because:
//	- nsTextWidget will be rewritten as an XP widget by the Editor team.
//	- menu handling will be rewritten by the XPApp team.
//-------------------------------------------------------------------------

#include <Scrap.h>
#include <PP_Messages.h>		// for PP standard menu commands
enum
{
	menu_Apple = 128,
	menu_File,
	menu_Edit
};


//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
nsTextWidget::nsTextWidget() : nsMacControl(), nsITextWidget(), Repeater()
{
  NS_INIT_REFCNT();
  strcpy(gInstanceClassName, "nsTextWidget");
  SetControlType(kControlEditTextProc);
	mIsPassword = PR_FALSE;
	mIsReadOnly = PR_FALSE;
	mTE = nsnull;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
nsTextWidget::~nsTextWidget()
{
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_IMETHODIMP nsTextWidget::Create(nsIWidget *aParent,
                      const nsRect &aRect,
                      EVENT_CALLBACK aHandleEventFunction,
                      nsIDeviceContext *aContext,
                      nsIAppShell *aAppShell,
                      nsIToolkit *aToolkit,
                      nsWidgetInitData *aInitData) 
{
	Inherited::Create(aParent, aRect, aHandleEventFunction,
						aContext, aAppShell, aToolkit, aInitData);

	// get the TEHandle
	if (mControl)
	{
		Size dataSize = sizeof(mTE);
		::GetControlData(mControl, kControlNoPart, kControlEditTextTEHandleTag, dataSize, (Ptr)&mTE, &dataSize);
	}
	return NS_OK;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
nsresult nsTextWidget::QueryInterface(REFNSIID aIID, void** aInstancePtr)
{
	if (NULL == aInstancePtr) {
	    return NS_ERROR_NULL_POINTER;
	}

  static NS_DEFINE_IID(kITextWidgetIID, NS_ITEXTWIDGET_IID);
  if (aIID.Equals(kITextWidgetIID)) {
    AddRef();
    *aInstancePtr = (void**)(nsITextWidget*)this;
    return NS_OK;
  }
  return Inherited::QueryInterface(aIID, aInstancePtr);
}

#pragma mark -
//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
PRBool nsTextWidget::DispatchMouseEvent(nsMouseEvent &aEvent)
{
	PRBool eventHandled = nsWindow::DispatchMouseEvent(aEvent);	// we don't want the 'Inherited' nsMacControl behavior

	if ((! eventHandled) && (mTE != nsnull))
	{
		if (aEvent.message == NS_MOUSE_LEFT_BUTTON_DOWN)
		{
			Point thePoint;
			thePoint.h = aEvent.point.x;
			thePoint.v = aEvent.point.y;

			short theModifiers;
			EventRecord* theOSEvent = (EventRecord*)aEvent.nativeMsg;
			if (theOSEvent)
			{
				theModifiers = theOSEvent->modifiers;
			}
			else
			{
				if (aEvent.isShift)		theModifiers = shiftKey;
				if (aEvent.isControl)	theModifiers |= controlKey;
				if (aEvent.isAlt)			theModifiers |= optionKey;
			}
			StartDraw();
			::HandleControlClick(mControl, thePoint, theModifiers, nil);
			EndDraw();
			eventHandled = PR_TRUE;
		}
	}
	return (eventHandled);
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
PRBool nsTextWidget::DispatchWindowEvent(nsGUIEvent &aEvent)
{
	// hack: if Enter is pressed, pass Return
	switch (aEvent.message)
	{
		case NS_KEY_DOWN:
		case NS_KEY_UP:
		{
  			nsKeyEvent* keyEvent = (nsKeyEvent*)&aEvent;
			if (keyEvent->keyCode == 0x03)
			{
				keyEvent->keyCode = NS_VK_RETURN;
				EventRecord* theOSEvent = (EventRecord*)aEvent.nativeMsg;
				if (theOSEvent)
					theOSEvent->message = (theOSEvent->message & ~charCodeMask) + NS_VK_RETURN;
			}
			break;
		}
	}

	// dispatch the message
	PRBool eventHandled = Inherited::DispatchWindowEvent(aEvent);

	// handle the message here if nobody else processed it already
	if ((! eventHandled) && (mTE != nsnull))
	{
		switch (aEvent.message)
		{
				case NS_MENU_SELECTED:
				{
					nsMenuEvent* menuEvent = (nsMenuEvent*)&aEvent;
					long menuID = HiWord(menuEvent->mCommand);
					long menuItem = LoWord(menuEvent->mCommand);
					switch (menuID)
					{
						case menu_Edit:
						{
							switch (menuItem)
							{
//								case cmd_Undo:

								case cmd_Cut:
								case cmd_Copy:
								{
									PRUint32 startSel = 0, endSel = 0;
									GetSelection ( &startSel, &endSel );
									if ( startSel != endSel ) {
										const Uint32 selectionLen = (endSel - startSel) + 1;
										
										// extract out the selection into a different nsString so
										// we can keep it unicode as long as possible
										PRUint32 unused = 0;
										nsString str, selection;
										GetText ( str, 0, unused );
										str.Mid ( selection, startSel, (endSel-startSel)+1 );
										
										// now |selection| holds the current selection in unicode.
										// We need to convert it to a c-string for MacOS.
										auto_ptr<char> cRepOfSelection ( new char[selection.Length() + 1] );
										selection.ToCString ( cRepOfSelection.get(), selectionLen );
										
										// copy it to the scrapMgr
										::ZeroScrap();
										::PutScrap ( selectionLen, 'TEXT', cRepOfSelection.get() );
									
										// if we're cutting, remove the text from the widget
										if ( menuItem == cmd_Cut ) {
											unused = 0;
											str.Cut ( startSel, selectionLen );
											SetText ( str, unused );
										}
									} // if there is a selection
									break;
								}
									
								case cmd_Paste:
								{
									long scrapOffset;
									Handle scrapH = ::NewHandle(0);
									long scrapLen = ::GetScrap(scrapH, 'TEXT', &scrapOffset);
									if (scrapLen > 0)
									{
										::HLock(scrapH);

										// truncate to the first line
										char* cr = strchr((char*)*scrapH, '\r');
										if (cr != nil)
											scrapLen = cr - *scrapH;

										// paste text
										nsString str;
										str.SetString((char*)*scrapH, scrapLen);
										PRUint32 startSel, endSel;
										GetSelection(&startSel, &endSel);
										PRUint32 outSize;
										InsertText(str, startSel, endSel, outSize);

										::HUnlock(scrapH);
									}
									::DisposeHandle(scrapH);
									break;
								}
								case cmd_Clear:
								{
									nsString str;
									PRUint32 outSize;
									SetText(str, outSize);
									break;
								}
								case cmd_SelectAll:
								{
									SelectAll();
									break;
								}
							}
							break;
						}
					}
					break;
				} // case NS_MENU_SELECTED

				case NS_GOTFOCUS:
				{
					StartDraw();
					//::TEActivate(mTE);
					ActivateControl(mControl);
					OSErr err = SetKeyboardFocus(mWindowPtr, mControl, kControlFocusNextPart);
						nsRect rect = mBounds;
						rect.x = rect.y = 0;
						Rect macRect;
						nsRectToMacRect(rect, macRect);
						::InsetRect(&macRect, 2, 2);
						::DrawThemeFocusRect(&macRect, true);
					EndDraw();
					StartIdling();
					break;
				}

				case NS_LOSTFOCUS:
				{
					StopIdling();
					StartDraw();
					//::TEDeactivate(mTE);
					OSErr err = SetKeyboardFocus(mWindowPtr, mControl, kControlFocusNoPart);
						nsRect rect = mBounds;
						rect.x = rect.y = 0;
						Rect macRect;
						nsRectToMacRect(rect, macRect);
						::InsetRect(&macRect, 2, 2);
						::DrawThemeFocusRect(&macRect, false);
					DeactivateControl(mControl);
					EndDraw();
					break;
				}

				case NS_KEY_DOWN:
				{
			  		char theChar;
			  		unsigned short theKey;
			  		unsigned short theModifiers;
			  		EventRecord* theOSEvent = (EventRecord*)aEvent.nativeMsg;
		  			nsKeyEvent* keyEvent = (nsKeyEvent*)&aEvent;
			  		if (theOSEvent)
			  		{
			  			theChar = (theOSEvent->message & charCodeMask);
			  			theKey  = (theOSEvent->message & keyCodeMask) >> 8;
			  			theModifiers = theOSEvent->modifiers;
			  		}
			  		else
			  		{
			  			theChar = keyEvent->keyCode;
			  			theKey  = 0;		// what else?
			  			if (keyEvent->isShift)		theModifiers = shiftKey;
			  			if (keyEvent->isControl)	theModifiers |= controlKey;
			  			if (keyEvent->isAlt)		theModifiers |= optionKey;

			  		}
			  		if (theChar != NS_VK_RETURN)	// don't pass Return: nsTextWidget is a single line editor
			  		{
						StartDraw();		  			
			  			::HandleControlKey(mControl, theKey, theChar, theModifiers);
						EndDraw();
			  			eventHandled = PR_TRUE;
			  		}
					break;
				}
		}
	}
	return (eventHandled);
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
PRBool nsTextWidget::OnPaint(nsPaintEvent &aEvent)
{
	Inherited::OnPaint(aEvent);
	
	/* draw a box.*/
	if (mVisible && mIsReadOnly)
	{
		nsRect ctlRect = mBounds;
		ctlRect.x = ctlRect.y = 0;
		//ctlRect.Deflate(1, 1);
		mTempRenderingContext->SetColor(NS_RGB(0, 0, 0));
		mTempRenderingContext->DrawRect(ctlRect);
	}
	
	return PR_FALSE;
}

#pragma mark -

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD nsTextWidget::SetPassword(PRBool aIsPassword)
{
	nsresult	theResult = NS_OK;
	
	// note: it is assumed that we can't have a read-only password field
	if (aIsPassword != mIsPassword)
	{
		mIsPassword = aIsPassword;
		short newControlType = (aIsPassword ? kControlEditTextPasswordProc : kControlEditTextProc);
		SetControlType(newControlType);
		
		theResult = CreateOrReplaceMacControl(newControlType);
	}
	return theResult;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::SetReadOnly(PRBool aReadOnlyFlag, PRBool& aOldFlag)
{
	nsresult	theResult = NS_OK;

	// note: it is assumed that we can't have a read-only password field
	aOldFlag = mIsReadOnly;
	if (aReadOnlyFlag != mIsReadOnly)
	{
		mIsReadOnly = aReadOnlyFlag;
		short newControlType = (aReadOnlyFlag ? kControlStaticTextProc : kControlEditTextProc);
		SetControlType(newControlType);

		theResult = CreateOrReplaceMacControl(newControlType);
	}
	return theResult;
}

#pragma mark -

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD nsTextWidget::SetMaxTextLength(PRUint32 aChars)
{
	//�TODO: install a kControlEditTextKeyFilterTag proc
  return NS_OK;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::GetText(nsString& aTextBuffer, PRUint32 /*aBufferSize*/, PRUint32& aSize) 
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	Size textSize;
	::GetControlDataSize(mControl, kControlNoPart, kControlEditTextTextTag, &textSize);

	char* str = new char[textSize];
	if (str)
	{
		ResType	tag = (mIsPassword ? kControlEditTextPasswordTag : kControlEditTextTextTag);
		::GetControlData(mControl, kControlNoPart, tag, textSize, (Ptr)str, &textSize);
	  aTextBuffer.SetLength(0);
	  aTextBuffer.Append(str, textSize);
		aSize = textSize;
		delete [] str;
	}
	else
	{
		aSize = 0;
		return NS_ERROR_OUT_OF_MEMORY;
	}

	return NS_OK;
}

//-------------------------------------------------------------------------
//
// Assumes aSize is |out| only and does not yet have a value.
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::SetText(const nsString& aText, PRUint32& outSize)
{
	outSize = aText.Length();
	const unsigned int bufferSize = outSize + 1;	// add 1 for null
	
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

		
	auto_ptr<char> str ( new char[bufferSize] );
	if ( str.get() )
	{
		aText.ToCString(str.get(), bufferSize);
		::SetControlData(mControl, kControlNoPart, kControlEditTextTextTag, outSize, (Ptr)str.get());
		Invalidate(PR_FALSE);
	}
	else
		return NS_ERROR_OUT_OF_MEMORY;

	return NS_OK;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::InsertText(const nsString &aText, PRUint32 aStartPos, PRUint32 aEndPos, PRUint32& aSize)
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	Size textSize;
	::GetControlDataSize(mControl, kControlNoPart, kControlEditTextTextTag, &textSize);

	nsString textStr;
	PRUint32 outTextSize;
	nsresult retVal = GetText(textStr, textSize, outTextSize);
	if (retVal != NS_OK)
		return retVal;

	if ((aStartPos == -1L) && (aEndPos == -1L))
	{
	  textStr.Append(aText);
	}
	else
	{
		if ((PRInt32)aStartPos < 0)
			aStartPos = 0;
		if ((PRInt32)aEndPos < 0)
			aStartPos = 0;
		if (aEndPos < aStartPos)
			aEndPos = aStartPos;
		textStr.Cut(aStartPos, aEndPos - aStartPos);
		textStr.Insert((nsString &)aText, aStartPos, aText.Length());
	}

  aSize = textStr.Length();
	return (SetText(textStr, aSize));
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::RemoveText()
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	StartDraw();
	::SetControlData(mControl, kControlNoPart, kControlEditTextTextTag, 0, (Ptr)"");
	EndDraw();
  return NS_OK;
}

#pragma mark -
//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD nsTextWidget::SelectAll()
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	Size textSize;
	::GetControlDataSize(mControl, kControlNoPart, kControlEditTextTextTag, &textSize);

  return (SetSelection(0, textSize));
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::SetSelection(PRUint32 aStartSel, PRUint32 aEndSel)
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	ControlEditTextSelectionRec textSelectionRec;
	textSelectionRec.selStart = aStartSel;
	textSelectionRec.selEnd   = aEndSel;

	StartDraw();
	::SetControlData(mControl, kControlNoPart, kControlEditTextSelectionTag,
											sizeof(textSelectionRec), (Ptr)&textSelectionRec);
	EndDraw();

  return NS_OK;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::GetSelection(PRUint32 *aStartSel, PRUint32 *aEndSel)
{
	if (!mControl)
		return NS_ERROR_NOT_INITIALIZED;

	ControlEditTextSelectionRec textSelectionRec;

	Size dataSize = sizeof(textSelectionRec);
	::GetControlData(mControl, kControlNoPart, kControlEditTextSelectionTag,
											dataSize, (Ptr)&textSelectionRec, &dataSize);

	*aStartSel = textSelectionRec.selStart;
	*aEndSel = textSelectionRec.selEnd;

  return NS_OK;
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD  nsTextWidget::SetCaretPosition(PRUint32 aPosition)
{
	return(SetSelection(aPosition, aPosition));
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
NS_METHOD nsTextWidget::GetCaretPosition(PRUint32& aPos)
{
	PRUint32 startSel, endSel;
	nsresult retVal = GetSelection(&startSel, &endSel);
	aPos = startSel;
  return retVal;
}

#pragma mark -
//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
void	nsTextWidget::RepeatAction(const EventRecord& inMacEvent)
{
	if (mTE)
	{
		StartDraw();
		//::TEIdle(mTE);
		IdleControls(mWindowPtr);	// this should really live in the window
									// but the problem is that we have to set
									// the graphic environment with StartDraw/EndDraw
									// in order to make the cursor blink
		EndDraw();
	}
}

//-------------------------------------------------------------------------
//
//
//-------------------------------------------------------------------------
void nsTextWidget::GetRectForMacControl(nsRect &outRect)
{
	outRect = mBounds;
	outRect.x = outRect.y = 0;
	// inset to make space for border
	if (mIsReadOnly)
		outRect.Deflate(1, 1);
	else
		outRect.Deflate(4, 4);
}

