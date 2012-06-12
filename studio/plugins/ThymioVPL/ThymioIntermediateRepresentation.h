#ifndef THYMIO_INTERMEDIATE_REPRESENTATION_H
#define THYMIO_INTERMEDIATE_REPRESENTATION_H

#include <vector>
#include <string>
#include <map>
#include <set>
#include <iostream>

namespace Aseba
{
	using namespace std;
	
	enum ThymioIRButtonName 
	{
		THYMIO_BUTTONS_IR = 0,
		THYMIO_PROX_IR,
		THYMIO_PROX_GROUND_IR,
		THYMIO_TAP_IR,
		THYMIO_CLAP_IR,
		THYMIO_MOVE_IR,
		THYMIO_COLOR_IR,
		THYMIO_CIRCLE_IR,
		THYMIO_SOUND_IR
	};
	
	enum ThymioIRErrorCode
	{
		THYMIO_NO_ERROR = 0,
		THYMIO_MISSING_EVENT,
		THYMIO_MISSING_ACTION,
		THYMIO_EVENT_NOT_SET,
		THYMIO_EVENT_MULTISET,
		THYMIO_INVALID_CODE
	};
	
	enum ThymioIRErrorType
	{
		THYMIO_NO_TYPE_ERROR = 0,
		THYMIO_SYNTAX_ERROR,
		THYMIO_TYPE_ERROR,
		THYMIO_CODE_ERROR
	};
	
	class ThymioIRVisitor;
	
	class ThymioIRButton 
	{
	public:
		ThymioIRButton(int size=0, ThymioIRButtonName n=THYMIO_BUTTONS_IR);
		~ThymioIRButton();

		void setClicked(int i, bool status);
		bool isClicked(int i);
		int size();

		ThymioIRButtonName getName();
		void setBasename(wstring n);
		wstring getBasename();
		
		bool isEventButton();
		bool isValid();

		void accept(ThymioIRVisitor *visitor);

	private:
		vector<bool> buttons;
		ThymioIRButtonName name;
		wstring basename;
	};
	
	class ThymioIRButtonSet
	{
	public:			
		ThymioIRButtonSet(ThymioIRButton *event=0, ThymioIRButton *action=0);
		
		void addEventButton(ThymioIRButton *event);
		void addActionButton(ThymioIRButton *action);
		
		ThymioIRButton *getEventButton();
		ThymioIRButton *getActionButton();
		
		bool hasEventButton();
		bool hasActionButton();
	
		void accept(ThymioIRVisitor *visitor);
	
	private:
		ThymioIRButton *eventButton;
		ThymioIRButton *actionButton;
	};
	
	class ThymioIRVisitor
	{
	public:
		ThymioIRVisitor() : errorCode(THYMIO_NO_ERROR) {}
		
		virtual void visit(ThymioIRButton *button);
		virtual void visit(ThymioIRButtonSet *buttonSet);

		ThymioIRErrorCode getErrorCode();
		bool isSuccessful();
		std::wstring getErrorMessage();		
		
	protected:
		ThymioIRErrorCode errorCode;
		wstring toWstring(int val);
	};

	class ThymioIRTypeChecker : public ThymioIRVisitor
	{
	public:
		ThymioIRTypeChecker() : ThymioIRVisitor(), activeActionName(THYMIO_BUTTONS_IR) {}
		~ThymioIRTypeChecker();
		
		virtual void visit(ThymioIRButton *button);
		virtual void visit(ThymioIRButtonSet *buttonSet);		
		
		void reset();
		void clear();
		
	private:
		ThymioIRButtonName activeActionName;
		
		multimap<wstring, ThymioIRButton*> moveHash;
		multimap<wstring, ThymioIRButton*> colorHash;
		multimap<wstring, ThymioIRButton*> circleHash;
		multimap<wstring, ThymioIRButton*> soundHash;

		set<ThymioIRButtonName> tapSeenActions;
		set<ThymioIRButtonName> clapSeenActions;
	};

	class ThymioIRSyntaxChecker : public ThymioIRVisitor
	{
	public:
		ThymioIRSyntaxChecker() : ThymioIRVisitor() {}
		~ThymioIRSyntaxChecker() {}
		
		virtual void visit(ThymioIRButton *button);
		virtual void visit(ThymioIRButtonSet *buttonSet); 
	
		void clear() {}
	};

	class ThymioIRCodeGenerator : public ThymioIRVisitor
	{
	public:
		ThymioIRCodeGenerator();
		~ThymioIRCodeGenerator();
		
		virtual void visit(ThymioIRButton *button);
		virtual void visit(ThymioIRButtonSet *buttonSet);

		vector<wstring>::const_iterator beginCode() { return generatedCode.begin(); }
		vector<wstring>::const_iterator endCode() { return generatedCode.end(); }
		void reset();
		void clear();
		
	private:
		map<ThymioIRButtonName, int> editor;
		vector<wstring> generatedCode;
		vector<wstring> directions;
		int currentBlock;
		bool inIfBlock;
	};

	class ThymioCompiler 
	{
	public:
		ThymioCompiler();
		~ThymioCompiler();
		
		void compile(int row);
		void generateCode();

		void AddButtonSet(ThymioIRButtonSet *set);
		void InsertButtonSet(int row, ThymioIRButtonSet *set);
		void RemoveButtonSet(int row);
		void ReplaceButtonSet(int row, ThymioIRButtonSet *set);

		ThymioIRErrorCode getErrorCode();
		std::wstring getErrorMessage();
		bool isSuccessful();
		
		vector<wstring>::const_iterator beginCode();
		vector<wstring>::const_iterator endCode(); 

		void clear();

	private:
		vector<ThymioIRButtonSet*> buttonSet;
		ThymioIRTypeChecker   typeChecker;
		ThymioIRSyntaxChecker syntaxChecker;
		ThymioIRCodeGenerator codeGenerator;
		
		ThymioIRErrorType errorType;
	};

}; // Aseba

#endif
